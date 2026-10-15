#include "export_metadata.h"
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QSet>
#include <QtEndian>
#include <libraw/libraw.h>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace Export {
namespace {
constexpr int cap=4*1024*1024;
struct Parser {
    QByteArray b; bool little=true,good=true; QSet<quint32> seen;
    bool range(quint64 p,quint64 n){if(p>quint64(b.size())||n>quint64(b.size())-p){good=false;return false;}return true;}
    quint16 u16(quint32 p){if(!range(p,2))return 0;return little?qFromLittleEndian<quint16>(b.constData()+p):qFromBigEndian<quint16>(b.constData()+p);}
    quint32 u32(quint32 p){if(!range(p,4))return 0;return little?qFromLittleEndian<quint32>(b.constData()+p):qFromBigEndian<quint32>(b.constData()+p);}
    void ifd(quint32 offset,int kind,PhotoMetadata &m,int depth=0){
        if(!offset)return;if(depth>2||seen.contains(offset)||!range(offset,2)){good=false;return;}seen.insert(offset);
        const int count=u16(offset);if(count>128||!range(quint64(offset)+2,quint64(count)*12+4)){good=false;return;}
        double lat[3]{},lon[3]{};bool hasLat=false,hasLon=false;QString latRef,lonRef;int altRef=0;
        for(int i=0;i<count&&good;++i){const quint32 e=offset+2+i*12;const int tag=u16(e),type=u16(e+2);const quint32 n=u32(e+4);int unit=0;
            switch(type){case 1:case 2:case 7:unit=1;break;case 3:unit=2;break;case 4:case 9:case 13:unit=4;break;case 5:case 10:unit=8;break;default:continue;}
            if(n>cap/quint32(unit)){good=false;break;}const quint64 bytes=quint64(n)*unit;const quint32 p=bytes<=4?e+8:u32(e+8);if(!range(p,bytes))break;
            const auto number=[&](int index=0)->double{if(index<0||quint32(index)>=n)return 0;const quint32 q=p+index*unit;if(type==1)return quint8(b[q]);if(type==3)return u16(q);if(type==4)return u32(q);if(type==5){const auto d=u32(q+4);return d?double(u32(q))/d:0;}return 0;};
            const auto text=[&]{auto x=b.mid(p,std::min(n,quint32(2048)));const int nul=x.indexOf('\0');if(nul>=0)x.truncate(nul);return QString::fromUtf8(x).trimmed();};
            if(kind==0){switch(tag){case 0x10f:m.make=text();break;case 0x110:m.model=text();break;case 0x13b:m.artist=text();break;case 0x8298:m.copyright=text();break;case 0x132:m.date=text();break;case 0x8769:if((type==4||type==13)&&n==1)ifd(u32(p),1,m,depth+1);break;case 0x8825:if((type==4||type==13)&&n==1)ifd(u32(p),2,m,depth+1);break;}}
            else if(kind==1){switch(tag){case 0x829a:m.exposure=number();break;case 0x829d:m.aperture=number();break;case 0x8827:m.iso=int(std::clamp(number(),0.,1000000.));break;case 0x920a:m.focalLength=number();break;case 0x9003:m.date=text();break;case 0xa434:m.lens=text();break;}}
            else if(kind==2){switch(tag){case 1:latRef=text();break;case 3:lonRef=text();break;case 2:if(type==5&&n==3){for(int j=0;j<3;++j)lat[j]=number(j);hasLat=true;}break;case 4:if(type==5&&n==3){for(int j=0;j<3;++j)lon[j]=number(j);hasLon=true;}break;case 5:altRef=int(number());break;case 6:m.altitude=number();break;}}
        }
        if(kind==2&&hasLat&&hasLon&&(latRef=="N"||latRef=="S")&&(lonRef=="E"||lonRef=="W")){
            m.latitude=(lat[0]+lat[1]/60+lat[2]/3600)*(latRef=="S"?-1:1);m.longitude=(lon[0]+lon[1]/60+lon[2]/3600)*(lonRef=="W"?-1:1);m.altitude*=altRef?-1:1;
            m.hasGps=std::abs(m.latitude)<=90&&std::abs(m.longitude)<=180&&std::isfinite(m.altitude);
        }
    }
    void read(PhotoMetadata &m){if(b.size()<8){good=false;return;}little=b.startsWith("II");if(!little&&!b.startsWith("MM")){good=false;return;}if(u16(2)!=42){good=false;return;}ifd(u32(4),0,m);}
};
QByteArray le16(quint16 v){QByteArray b(2,0);qToLittleEndian(v,b.data());return b;}
QByteArray le32(quint32 v){QByteArray b(4,0);qToLittleEndian(v,b.data());return b;}
struct Entry{quint16 tag,type;quint32 count;QByteArray value;};
// Canon CR3 stores IFD0 in CMT1 under moov / Canon UUID. Read only that
// metadata box, never scan the image payload for tag-like byte patterns.
void cr3Copyright(const QString &path,PhotoMetadata &m){
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return;int boxes=0;
    const auto walk=[&](auto &&self,qint64 begin,qint64 end,int depth)->void{
        if(depth>2)return;
        for(qint64 p=begin;p+8<=end&&++boxes<=4096;){
            if(!f.seek(p))return;auto header=f.read(8);if(header.size()!=8)return;
            quint64 length=qFromBigEndian<quint32>(header.constData());qint64 head=8;
            if(length==1){auto ext=f.read(8);if(ext.size()!=8)return;length=qFromBigEndian<quint64>(ext.constData());head=16;}
            if(!length)length=quint64(end-p);if(length<quint64(head)||length>quint64(end-p))return;
            const auto type=header.mid(4,4);const auto content=p+head,limit=p+qint64(length);
            if(depth==0&&type=="moov")self(self,content,limit,1);
            else if(depth==1&&type=="uuid"&&length>=quint64(head+16)){
                f.seek(content);const auto uuid=f.read(16);
                if(uuid==QByteArray::fromHex("85c0b687820f11e08111f4ce462b6a48"))self(self,content+16,limit,2);
            }else if(depth==2&&type=="CMT1"&&length-quint64(head)<=cap){
                f.seek(content);Parser parser{f.read(qint64(length)-head)};PhotoMetadata tags;parser.read(tags);
                if(parser.good){if(!tags.artist.isEmpty())m.artist=tags.artist;m.copyright=tags.copyright;}return;
            }
            p=limit;
        }
    };walk(walk,0,f.size(),0);
}
Entry text(quint16 tag,QString v){auto b=v.left(2048).toUtf8();b.replace('\0',' ');b+='\0';return {tag,2,quint32(b.size()),b};}
Entry shortValue(quint16 tag,quint16 value){return {tag,3,1,le16(value)};}
Entry longValue(quint16 tag,quint32 value){return {tag,4,1,le32(value)};}
QByteArray rational(double value){if(!std::isfinite(value)||value<0)value=0;const double den=value>4000?100:1000000;return le32(quint32(std::min(value*den,4294967295.)))+le32(quint32(den));}
Entry rat(quint16 tag,double value){return {tag,5,1,rational(value)};}
quint32 appendIfd(QByteArray &b,QList<Entry> entries,QMap<quint16,quint32> *positions=nullptr){
    std::sort(entries.begin(),entries.end(),[](auto &a,auto &c){return a.tag<c.tag;});
    if(b.size()%2)b+='\0';const auto offset=quint32(b.size());b+=le16(quint16(entries.size()));b+=QByteArray(entries.size()*12+4,0);
    int i=0;for(const auto &e:entries){const int p=offset+2+i++*12;b.replace(p,2,le16(e.tag));b.replace(p+2,2,le16(e.type));b.replace(p+4,4,le32(e.count));
        if(e.value.size()<=4){QByteArray v=e.value;v.resize(4);for(int n=e.value.size();n<4;++n)v[n]=0;b.replace(p+8,4,v);}else{if(b.size()%2)b+='\0';b.replace(p+8,4,le32(quint32(b.size())));b+=e.value;}if(positions)(*positions)[e.tag]=p+8;}
    return offset;
}
}
PhotoMetadata readMetadata(const QString &source,Metadata policy,bool gps,QString &error){
    error.clear();PhotoMetadata m;if(policy==Metadata::None&&!gps)return m;
    if(QFileInfo(source).suffix().compare("cr3",Qt::CaseInsensitive)==0){
        LibRaw raw;int code;
#ifdef Q_OS_WIN
        code=raw.open_file(reinterpret_cast<const wchar_t *>(source.utf16()));
#else
        code=raw.open_file(QFile::encodeName(source).constData());
#endif
        if(code!=LIBRAW_SUCCESS){error=QStringLiteral("RAW元数据读取失败");return {};}
        const auto &o=raw.imgdata.other;const auto bounded=[](const char *v,size_t n){return QString::fromUtf8(v,int(strnlen(v,n))).left(2048);};
        m.make=bounded(raw.imgdata.idata.make,sizeof(raw.imgdata.idata.make));m.model=bounded(raw.imgdata.idata.model,sizeof(raw.imgdata.idata.model));
        m.artist=bounded(o.artist,sizeof(o.artist));m.lens=bounded(raw.imgdata.lens.Lens,sizeof(raw.imgdata.lens.Lens));
        m.exposure=o.shutter;m.aperture=o.aperture;m.focalLength=o.focal_len;m.iso=int(o.iso_speed);
        if(o.timestamp>0)m.date=QDateTime::fromSecsSinceEpoch(o.timestamp).toString("yyyy:MM:dd HH:mm:ss");
        cr3Copyright(source,m);
        const auto &g=o.parsed_gps;if(g.gpsparsed){m.latitude=(g.latitude[0]+g.latitude[1]/60.+g.latitude[2]/3600.)*(g.latref=='S'?-1:1);m.longitude=(g.longitude[0]+g.longitude[1]/60.+g.longitude[2]/3600.)*(g.longref=='W'?-1:1);m.altitude=g.altitude*(g.altref?-1:1);m.hasGps=std::isfinite(m.latitude)&&std::isfinite(m.longitude)&&std::abs(m.latitude)<=90&&std::abs(m.longitude)<=180;}
    }else{
        QFile f(source);if(!f.open(QIODevice::ReadOnly)){error=f.errorString();return {};}
        auto head=f.read(2);QByteArray tiff;
        if(head==QByteArray::fromHex("ffd8")){
            while(f.pos()<16*1024*1024&&!f.atEnd()){char lead=0,mark=0;if(!f.getChar(&lead)||quint8(lead)!=255){error="EXIF JPEG marker";return {};}
                do{if(!f.getChar(&mark)){error="EXIF JPEG EOF";return {};}}while(quint8(mark)==255);
                if(quint8(mark)==0xda||quint8(mark)==0xd9)break;
                const auto l=f.read(2);if(l.size()!=2){error="EXIF JPEG length";return {};}
                const int n=(quint8(l[0])<<8)+quint8(l[1])-2;if(n<0){error="EXIF JPEG length";return {};}
                auto data=f.read(n);if(data.size()!=n){error="EXIF JPEG segment";return {};}
                if(quint8(mark)==0xe1&&data.startsWith(QByteArray("Exif\0\0",6))){tiff=data.mid(6);break;}
            }
        }else if(head=="II"||head=="MM"){f.seek(0);tiff=f.read(cap);}
        if(!tiff.isEmpty()){Parser parser{tiff};parser.read(m);if(!parser.good){error=QStringLiteral("损坏或超限的EXIF，未导出");return {};}}
    }
    if(policy!=Metadata::Camera){const auto artist=m.artist,copy=m.copyright;const bool has=m.hasGps;const auto lat=m.latitude,lon=m.longitude,alt=m.altitude;m={};if(policy==Metadata::Copyright){m.artist=artist;m.copyright=copy;}if(gps){m.hasGps=has;m.latitude=lat;m.longitude=lon;m.altitude=alt;}}
    if(!gps)m.hasGps=false;return m;
}
QByteArray exifBlock(const PhotoMetadata &m,QSize output,double ppi){
    QByteArray b("II*\0\10\0\0\0",8);
    QList<Entry> root{shortValue(0x112,1),rat(0x11a,ppi),rat(0x11b,ppi),shortValue(0x128,2),text(0x131,"LuminaS"),longValue(0x100,output.width()),longValue(0x101,output.height()),longValue(0x8769,0)};
    const auto add=[&](QList<Entry> &v,quint16 tag,const QString &s){if(!s.isEmpty())v<<text(tag,s);};
    add(root,0x10f,m.make);add(root,0x110,m.model);add(root,0x13b,m.artist);add(root,0x8298,m.copyright);add(root,0x132,m.date);
    if(m.hasGps)root<<longValue(0x8825,0);
    QMap<quint16,quint32> patch;appendIfd(b,root,&patch);
    QList<Entry> exif{longValue(0xa002,output.width()),longValue(0xa003,output.height()),{0x9000,7,4,QByteArray("0232",4)}};
    if(m.iso>0)exif<<shortValue(0x8827,quint16(std::min(m.iso,65535)));if(m.exposure>0)exif<<rat(0x829a,m.exposure);if(m.aperture>0)exif<<rat(0x829d,m.aperture);if(m.focalLength>0)exif<<rat(0x920a,m.focalLength);
    add(exif,0xa434,m.lens);add(exif,0x9003,m.date);b.replace(patch[0x8769],4,le32(appendIfd(b,exif)));
    if(m.hasGps){const auto dms=[](double value){value=std::abs(value);const auto deg=std::floor(value),minutes=std::floor((value-deg)*60);return rational(deg)+rational(minutes)+rational(((value-deg)*60-minutes)*60);};
        QList<Entry> g{{0,1,4,QByteArray::fromHex("02030000")},text(1,m.latitude<0?"S":"N"),{2,5,3,dms(m.latitude)},text(3,m.longitude<0?"W":"E"),{4,5,3,dms(m.longitude)},{5,1,1,QByteArray(1,m.altitude<0?1:0)},rat(6,std::abs(m.altitude))};
        b.replace(patch[0x8825],4,le32(appendIfd(b,g)));
    }return b;
}
}
