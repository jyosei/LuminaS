#pragma once
#include <QWidget>
#include <QImage>
#include <array>
class PreviewHistogram : public QWidget {
public:
    explicit PreviewHistogram(QWidget *parent=nullptr);
    void setImage(const QImage &image);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    std::array<std::array<int,256>,3> bins{};
    int samples=0,black=0,white=0;
};
