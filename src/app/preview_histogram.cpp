#include "preview_histogram.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <algorithm>
PreviewHistogram::PreviewHistogram(QWidget *parent):QWidget(parent){setMinimumHeight(96);setMaximumHeight(110);setObjectName("preview-histogram");}
void PreviewHistogram::setImage(const QImage &image){
    bins={};samples=black=white=0;
    if(!image.isNull()){
        const auto rgba=image.format()==QImage::Format_RGBA8888?image:image.convertToFormat(QImage::Format_RGBA8888);
        const int step=std::max(1,int(std::ceil(std::sqrt(double(rgba.width())*rgba.height()/16384))));
        for(int y=step/2;y<rgba.height();y+=step){const auto *row=rgba.constScanLine(y);
            for(int x=step/2;x<rgba.width();x+=step){const auto *p=row+4*x;if(p[3]==0)continue;++samples;
                for(int c=0;c<3;++c)++bins[c][p[c]];
                if(std::min({p[0],p[1],p[2]})<=1)++black;if(std::max({p[0],p[1],p[2]})>=254)++white;
            }
        }
    }
    setToolTip(QStringLiteral("sRGB显示预览抽样 %1 点；任一通道接近黑/白端：%2% / %3%。这是显示剪切提示，不是RAW传感器饱和或全源直方图。").arg(samples).arg(samples?black*100./samples:0,0,'f',1).arg(samples?white*100./samples:0,0,'f',1));update();
}
void PreviewHistogram::paintEvent(QPaintEvent *){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),palette().color(QPalette::Base));
    p.setPen(palette().color(QPalette::Text));p.drawText(QRect(6,0,width()-12,22),Qt::AlignLeft|Qt::AlignVCenter,QStringLiteral("直方图 · sRGB预览"));
    const QRectF plot(6,26,width()-12,height()-32);p.setPen(palette().color(QPalette::Mid));p.drawRect(plot);
    int maximum=1;for(const auto &channel:bins)for(int count:channel)maximum=std::max(maximum,count);
    const QColor colors[]{QColor("#d88888"),QColor("#88bc97"),QColor("#90a9d7")};
    if(samples)for(int c=0;c<3;++c){QPainterPath path;for(int i=0;i<256;++i){const QPointF point(plot.left()+i*plot.width()/255,plot.bottom()-std::log1p(bins[c][i])/std::log1p(maximum)*plot.height());if(i==0)path.moveTo(point);else path.lineTo(point);}p.setPen(QPen(colors[c],1));p.drawPath(path);}
    p.setPen(palette().color(QPalette::Text));p.drawText(QRect(width()-160,0,154,22),Qt::AlignRight|Qt::AlignVCenter,QStringLiteral("黑 %1%  白 %2%").arg(samples?black*100./samples:0,0,'f',1).arg(samples?white*100./samples:0,0,'f',1));
}
