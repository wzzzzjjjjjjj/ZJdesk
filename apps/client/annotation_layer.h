#pragma once

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

class AnnotationLayer final : public QWidget
{
    Q_OBJECT
public:
    explicit AnnotationLayer(QWidget *parent = nullptr, bool desktop = false);
    void setAnnotations(const QJsonArray &strokes, const QJsonObject &author = {});
    void setTool(const QString &tool);
    QString tool() const { return tool_; }
    void cancelDraft();
    void setAnnotationsInVideo(bool embedded) { embedded_ = embedded; update(); }

signals:
    void strokeReady(const QJsonObject &stroke);
    void drawingExitRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    QJsonArray strokes_;
    QJsonObject author_, draft_;
    QString tool_;
    QTimer publish_;
    bool drawing_ = false;
    bool embedded_ = false;
    bool desktop_ = false;
    QPointF normalized(const QPointF &point) const;
    void appendPoint(const QPointF &point);
};
