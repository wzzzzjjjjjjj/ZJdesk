#include "camera_permission.h"
#include "camera_permission_p.h"
#include <QScopeGuard>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#include <roapi.h>
#include <windows.system.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#endif

namespace zjdesk
{
#ifdef Q_OS_WIN
namespace
{
struct CameraSettingsResult
{
    bool opened = false;
    QString error;
};

CameraSettingsResult settingsError(HRESULT error)
{
    return {false, QStringLiteral("Windows 错误：0x%1。").arg(
        static_cast<quint32>(error), 8, 16, QLatin1Char('0'))};
}

CameraSettingsResult launchCameraSettings()
{
    const auto initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized)) return settingsError(initialized);
    const auto cleanup = qScopeGuard([] { RoUninitialize(); });
    using namespace ABI::Windows::Foundation;
    using namespace Microsoft::WRL;
    ComPtr<IUriRuntimeClassFactory> uriFactory;
    auto result = RoGetActivationFactory(Wrappers::HStringReference(
        RuntimeClass_Windows_Foundation_Uri).Get(), IID_PPV_ARGS(&uriFactory));
    if (FAILED(result)) return settingsError(result);
    ComPtr<IUriRuntimeClass> uri;
    result = uriFactory->CreateUri(Wrappers::HStringReference(L"ms-settings:privacy-webcam").Get(), &uri);
    if (FAILED(result)) return settingsError(result);
    ComPtr<ABI::Windows::System::ILauncherStatics> launcher;
    result = RoGetActivationFactory(Wrappers::HStringReference(
        RuntimeClass_Windows_System_Launcher).Get(), IID_PPV_ARGS(&launcher));
    if (FAILED(result)) return settingsError(result);
    const auto event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!event) return settingsError(HRESULT_FROM_WIN32(GetLastError()));
    const auto finished = std::shared_ptr<void>(event, [](void *handle) { CloseHandle(handle); });
    ComPtr<IAsyncOperation<bool>> operation;
    result = launcher->LaunchUriAsync(uri.Get(), &operation);
    if (FAILED(result)) return settingsError(result);
    using Handler = Implements<RuntimeClassFlags<ClassicCom>, IAsyncOperationCompletedHandler<bool>, FtmBase>;
    auto handler = Callback<Handler>([finished](IAsyncOperation<bool> *, AsyncStatus) -> HRESULT {
        SetEvent(finished.get());
        return S_OK;
    });
    if (!handler) return settingsError(E_OUTOFMEMORY);
    result = operation->put_Completed(handler.Get());
    if (FAILED(result)) return settingsError(result);
    const auto waited = WaitForSingleObject(finished.get(), 15000);
    if (waited != WAIT_OBJECT_0)
    {
        const auto error = waited == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        ComPtr<IAsyncInfo> info;
        if (SUCCEEDED(operation.As(&info))) info->Cancel();
        return settingsError(HRESULT_FROM_WIN32(error));
    }
    boolean opened = false;
    result = operation->GetResults(&opened);
    if (FAILED(result)) return settingsError(result);
    return {opened != false, opened ? QString{} : QStringLiteral("Windows 未能打开摄像头设置页。")};
}
} // namespace
#endif

CameraPermission cameraPermission()
{
#ifdef Q_OS_WIN
    // This unpackaged desktop app uses the device and desktop privacy switches.
    // DeviceAccessInformation reported DeniedBySystem even with both enabled.
    // These are read-only hints; the capture backend still enforces actual access.
    const QSettings device(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam"), QSettings::NativeFormat);
    const QSettings desktop(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\webcam\\NonPackaged"), QSettings::NativeFormat);
    return detail::desktopCameraPermission(device.value("Value").toString(), desktop.value("Value").toString());
#else
    // Linux device access is checked by the capture backend when opening the device.
    return CameraPermission::Allowed;
#endif
}

void openCameraPrivacySettings(QObject *context, CameraSettingsCompletion completed)
{
#ifdef Q_OS_WIN
    const auto result = std::make_shared<CameraSettingsResult>();
    auto *worker = QThread::create([result] { *result = launchCameraSettings(); });
    QObject::connect(worker, &QThread::finished, context, [result, completed] {
        completed(result->opened, result->error);
    });
    QObject::connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    worker->start();
#else
    QTimer::singleShot(0, context, [completed] {
        completed(false, QStringLiteral("当前系统不支持 Windows 摄像头设置。"));
    });
#endif
}
} // namespace zjdesk
