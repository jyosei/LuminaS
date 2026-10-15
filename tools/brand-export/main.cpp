#include <QApplication>
#include <QBuffer>
#include <QDataStream>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QSvgRenderer>
#include <QTextStream>

// Development utility: render the original vector mark into a Windows multi-size
// icon and a review PNG. Refuses existing outputs; never linked into the app.
int main(int argc,char **argv){
    QApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()!=4){QTextStream(stderr)<<"usage: brand_export source.svg fresh.ico fresh.png\n";return 2;}
    if(QFileInfo::exists(args[2])||QFileInfo::exists(args[3]))return 3;
    QSvgRenderer renderer(args[1]);if(!renderer.isValid())return 4;
    QList<QByteArray> images;const QList<int> sizes{16,24,32,48,64,128,256};
    for(int size:sizes){QImage image(size,size,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);renderer.render(&painter);painter.end();
        QByteArray bytes;QBuffer buffer(&bytes);if(!buffer.open(QIODevice::WriteOnly)||!image.save(&buffer,"PNG"))return 5;images<<bytes;
        if(size==256&&!image.save(args[3],"PNG"))return 6;
    }
    QSaveFile output(args[2]);if(!output.open(QIODevice::WriteOnly))return 7;
    QDataStream stream(&output);stream.setByteOrder(QDataStream::LittleEndian);
    stream<<quint16(0)<<quint16(1)<<quint16(sizes.size());quint32 offset=6+16*quint32(sizes.size());
    for(int i=0;i<sizes.size();++i){const quint8 dimension=sizes[i]==256?0:quint8(sizes[i]);
        stream<<dimension<<dimension<<quint8(0)<<quint8(0)<<quint16(1)<<quint16(32)<<quint32(images[i].size())<<offset;offset+=quint32(images[i].size());}
    for(const auto &bytes:images)if(stream.writeRawData(bytes.constData(),int(bytes.size()))!=bytes.size())return 8;
    return stream.status()==QDataStream::Ok&&output.commit()?0:9;
}
