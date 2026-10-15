#include "ui_primitives.h"
#include "color/display_transform.h"
#include <QColorSpace>
#include <QApplication>
#include <QIconEngine>
#include <QSvgRenderer>
#include <QFile>
#include <QPainter>
#include <QtMath>
#include <QPainterPath>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QLabel>
#include <QListWidget>
#include <QStyledItemDelegate>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontInfo>

static void initializeBrandResources(){Q_INIT_RESOURCE(brand_fonts);}
static void initializeInterfaceResources(){Q_INIT_RESOURCE(interface_icons);}

namespace Ui {
QFont brandFont(int pixels){
    static const QString family=[]{initializeBrandResources();const int id=QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/SourceSerif4-Regular.ttf"));return QFontDatabase::applicationFontFamilies(id).value(0,QStringLiteral("Cambria"));}();
    QFont font(family);font.setPixelSize(pixels);font.setWeight(QFont::Normal);font.setLetterSpacing(QFont::AbsoluteSpacing,.2);return font;
}
static void ensureIcons(){static const bool initialized=[]{initializeInterfaceResources();return true;}();Q_UNUSED(initialized);}
QIcon brandIcon(){
    ensureIcons();static const QIcon result=[]{QIcon icon;QSvgRenderer renderer(QStringLiteral(":/icons/brand-mark.svg"));
        for(int size:{16,24,32,48,64,128,256}){QPixmap pixels(size,size);pixels.fill(Qt::transparent);QPainter painter(&pixels);renderer.render(&painter);painter.end();icon.addPixmap(pixels);}return icon;}();return result;
}
// Official Lucide source assets. Vector paint stays crisp and follows the current
// application palette without changing any photograph or display colour transform.
class GlyphEngine final:public QIconEngine {
public:
    explicit GlyphEngine(Glyph g):glyph(g){}
    QIconEngine *clone() const override{return new GlyphEngine(glyph);}
    void paint(QPainter *p,const QRect &r,QIcon::Mode mode,QIcon::State) override {
        static const char *names[]={"image-plus","folder-open","layout-grid","sliders-horizontal","undo-2","redo-2","chevron-left","chevron-right","maximize","hand","crop","columns-2","check","x","circle","panels-top-left","sun-moon","rotate-ccw","sun","palette","chart-spline","thermometer","chevron-down"};
        const auto group=mode==QIcon::Disabled?QPalette::Disabled:QPalette::Active;
        const auto colour=QApplication::palette().color(group,QPalette::ButtonText);
        QFile source(QStringLiteral(":/icons/%1.svg").arg(QString::fromLatin1(names[int(glyph)])));
        if(!source.open(QIODevice::ReadOnly))return;
        auto bytes=source.readAll();bytes.replace("currentColor",colour.name().toUtf8());
        QSvgRenderer renderer(bytes);p->save();p->setRenderHint(QPainter::Antialiasing);renderer.render(p,QRectF(r));p->restore();
    }
    QPixmap pixmap(const QSize &size,QIcon::Mode mode,QIcon::State state) override {QPixmap result(size);result.fill(Qt::transparent);QPainter p(&result);paint(&p,QRect(QPoint(),size),mode,state);return result;}
private:Glyph glyph;
};
QIcon icon(Glyph g){ensureIcons();return QIcon(new GlyphEngine(g));}
void applyTheme(QWidget *window,bool dark){
    window->setStyleSheet(QString());
    const QColor shell(dark?"#252528":"#e7e7e7"),panel(dark?"#2c2c31":"#f6f6f6"),text(dark?"#ededed":"#242424");
    const QColor muted(dark?"#b5b5b5":"#5b5b5b"),line(dark?"#414141":"#c9c9c9"),hover(dark?"#383838":"#ececec"),pressed(dark?"#232327":"#dedede");
    const QColor edge(dark?"#555555":"#b0b0b0"),accent(dark?"#b9a2dd":"#74579c"),accentSoft(dark?"#3c3448":"#e9e0f3");
    // Photoshop/Lightroom-style restrained violet: only focused or selected rows.
    const QColor selectedText(dark?"#ffffff":"#2a2035");
    QPalette palette;
    palette.setColor(QPalette::Window,shell);palette.setColor(QPalette::WindowText,text);palette.setColor(QPalette::Base,panel);palette.setColor(QPalette::AlternateBase,shell);
    palette.setColor(QPalette::Text,text);palette.setColor(QPalette::Button,panel);palette.setColor(QPalette::ButtonText,text);
    palette.setColor(QPalette::Light,edge);palette.setColor(QPalette::Midlight,line);palette.setColor(QPalette::Mid,line);palette.setColor(QPalette::Dark,line);
    palette.setColor(QPalette::Highlight,accent);
    // Flat neutral selection can read dark-on-dark; keep the selected text legible.
    palette.setColor(QPalette::HighlightedText,dark?QColor("#121212"):QColor("#ffffff"));
    palette.setColor(QPalette::ToolTipBase,panel);palette.setColor(QPalette::ToolTipText,text);
    palette.setColor(QPalette::Disabled,QPalette::Text,muted);palette.setColor(QPalette::Disabled,QPalette::ButtonText,muted);
    palette.setColor(QPalette::Disabled,QPalette::WindowText,muted);palette.setColor(QPalette::Disabled,QPalette::Highlight,line);
    palette.setColor(QPalette::Disabled,QPalette::HighlightedText,muted);
    // Icons use the application palette, while all widgets inherit the window palette.
    QApplication::setPalette(palette);window->setPalette(palette);
    // Indicators must be painted *after* the palette is installed, because the
    // painting helper reads the active application palette; QFontInfo resolves the
    // native system family used by the brand label.
    const QString family=brandFont().family();
    // %1 text %2 muted %3 shell %4 panel %5 separator %6 base field %7 hover %8 pressed
    // %9 focus accent %10 selected background %11 selected text %12 edge %13 system family
    window->setStyleSheet(QStringLiteral(
        "QWidget{color:%1;} QWidget:disabled{color:%2;} "
        "QMainWindow,QWidget#photo-workspace,QStatusBar{background:%3;} QDialog{background:%3;} "
        // Flat toolbar and separators: no purple surface, no rounded bordered blocks.
        "QToolBar{background:%3;border:0;border-bottom:1px solid %5;spacing:4px;padding:4px 6px;} QToolBar::separator{width:1px;background:%5;margin:5px 6px;} "
        "QLabel#brand{font-family:'%13';font-size:22px;font-weight:400;padding:0 14px 0 2px;} QLabel[role=muted]{color:%2;} "
        // Sectional inspector: every section is a flat band split by a separator line,
        // not a filled rounded card.
        "QFrame#adjustment-card{background:%4;border:0;border-bottom:1px solid %5;border-radius:0;margin:0;} "
        "QToolButton#card-header{min-height:32px;padding:0 8px;border:0;border-radius:0;background:transparent;font-weight:600;text-align:left;} "
        "QToolButton#card-header:hover{background:%7;} QToolButton#card-header:pressed{background:%8;} "
        "QToolButton#card-header:checked{background:transparent;} QToolButton#card-header:checked:hover{background:%7;} "
        "QToolButton#card-header:focus{border:1px solid %9;} "
        "QWidget#adjustment-panel{background:%3;} QWidget#card-body{background:transparent;margin:0;} "
        "QFrame#context-bar,QFrame#filmstrip-header{background:%3;border:0;margin:0;} "
        // Explicit interactive states for every control the inspector uses.
        "QPushButton{min-height:28px;padding:0 10px;background:%4;border:1px solid %12;border-top-color:%12;border-bottom-color:%3;border-radius:4px;} "
        "QToolButton[workspaceTab=true]{border:0;border-bottom:2px solid transparent;border-radius:0;background:transparent;padding:6px 10px;} QToolButton[workspaceTab=true]:checked{border-bottom-color:%9;background:transparent;color:%1;} "
        "QPushButton:hover{background:%7;} QPushButton:pressed{background:%8;border-top-color:%3;border-bottom-color:%12;} QPushButton:checked{background:%10;border-color:%9;color:%11;} "
        "QPushButton:focus{border:1px solid %9;} QPushButton:disabled{background:%4;border-color:%5;color:%2;} "
        "QToolButton{min-width:28px;min-height:28px;padding:2px;border:1px solid transparent;border-radius:4px;background:transparent;} "
        "QToolButton:hover{background:%7;border-color:%5;} QToolButton:pressed{background:%8;} "
        "QToolButton:checked{background:%10;border-color:%9;color:%11;} "
        "QToolButton:focus{border:1px solid %9;} QToolButton:disabled{background:transparent;border-color:transparent;color:%2;} "
        "QComboBox{min-height:28px;padding:0 22px 0 8px;background:%6;border:1px solid %12;border-radius:4px;} "
        "QComboBox:hover{border-color:%9;} QComboBox:on{background:%6;border-color:%9;} QComboBox:focus{border:1px solid %9;} "
        "QComboBox:disabled{background:%4;border-color:%5;color:%2;} "
        "QComboBox::drop-down{subcontrol-origin:padding;subcontrol-position:top right;width:20px;border:0;background:transparent;} "
        "QComboBox::down-arrow{width:9px;height:9px;image:url(:/ui/spin-down-%14.xpm);} "
        "QComboBox::down-arrow:disabled,QComboBox::down-arrow:off{image:url(:/ui/spin-down-%14-disabled.xpm);} "
        "QComboBox QAbstractItemView{background:%4;border:1px solid %5;outline:0;selection-background-color:%10;selection-color:%11;} "
        "QLineEdit,QDoubleSpinBox,QSpinBox{min-height:28px;padding:0 6px;background:%6;border:1px solid %12;border-radius:4px;selection-background-color:%10;selection-color:%11;} "
        "QLineEdit:hover,QDoubleSpinBox:hover,QSpinBox:hover{border-color:%9;} "
        "QLineEdit:focus,QDoubleSpinBox:focus,QSpinBox:focus{border:1px solid %9;} "
        "QLineEdit:disabled,QDoubleSpinBox:disabled,QSpinBox:disabled{background:%4;border-color:%5;color:%2;} "
        // Spin resource arrows keep a usable 28x28 mouse target: 14px arrow button next
        // to the 14px text field area, stacked two high over the control height.
        "QDoubleSpinBox,QSpinBox{padding-right:17px;} "
        "QDoubleSpinBox::up-button,QSpinBox::up-button{width:14px;height:14px;top:0;right:1px;margin:0;background:%6;border:0;border-left:1px solid %5;border-bottom:1px solid %5;} "
        "QDoubleSpinBox::down-button,QSpinBox::down-button{width:14px;height:14px;bottom:0;right:1px;margin:0;background:%6;border:0;border-left:1px solid %5;} "
        "QDoubleSpinBox::up-button:hover,QSpinBox::up-button:hover,QDoubleSpinBox::down-button:hover,QSpinBox::down-button:hover{background:%7;} "
        "QDoubleSpinBox::up-button:pressed,QSpinBox::up-button:pressed,QDoubleSpinBox::down-button:pressed,QSpinBox::down-button:pressed{background:%8;} "
        "QDoubleSpinBox::up-button:disabled,QSpinBox::up-button:disabled,QDoubleSpinBox::down-button:disabled,QSpinBox::down-button:disabled{background:%4;} "
        "QDoubleSpinBox::up-button:off,QSpinBox::up-button:off,QDoubleSpinBox::down-button:off,QSpinBox::down-button:off{background:%4;} "
        "QDoubleSpinBox::up-arrow,QSpinBox::up-arrow{width:7px;height:7px;image:url(:/ui/spin-up-%14.xpm);} "
        "QDoubleSpinBox::down-arrow,QSpinBox::down-arrow{width:7px;height:7px;image:url(:/ui/spin-down-%14.xpm);} "
        "QDoubleSpinBox::up-arrow:disabled,QDoubleSpinBox::up-arrow:off,QSpinBox::up-arrow:disabled,QSpinBox::up-arrow:off{image:url(:/ui/spin-up-%14-disabled.xpm);} "
        "QDoubleSpinBox::down-arrow:disabled,QDoubleSpinBox::down-arrow:off,QSpinBox::down-arrow:disabled,QSpinBox::down-arrow:off{image:url(:/ui/spin-down-%14-disabled.xpm);} "
        "QProgressBar{background:%5;border:0;border-radius:2px;} QProgressBar::chunk{background:%9;border-radius:2px;} QSlider:focus{border:1px solid %9;border-radius:4px;} "
        "QSlider::groove:horizontal{height:2px;background:%5;border-radius:1px;} "
        "QSlider::sub-page:horizontal{background:%9;border-radius:1px;} "
        "QSlider::handle:horizontal{width:12px;height:12px;margin:-5px 0;background:%1;border:1px solid %12;border-radius:6px;} "
        "QSlider::handle:horizontal:hover{background:%7;border-color:%9;} QSlider::handle:horizontal:pressed{background:%8;border-color:%9;} "
        "QSlider::handle:horizontal:focus{border:1px solid %9;} "
        "QSlider:disabled::groove:horizontal{background:%5;} QSlider:disabled::sub-page:horizontal{background:%5;} QSlider:disabled::handle:horizontal{background:%4;border-color:%5;} "
        // QSS has no :indeterminate; the partial state paints through QPalette::Button.
        "QCheckBox{min-height:28px;spacing:6px;} QCheckBox:hover{color:%1;} "
        "QListWidget{background:%4;border:0;outline:0;} "
        "QListWidget::item{color:%1;} "
        "QListWidget::item:hover{background:%7;} QListWidget::item:pressed{background:%8;} QListWidget::item:selected{background:%10;color:%11;border:1px solid %9;} "
        "QListView::item{color:%1;} QListView::item:hover{background:%7;} QListView::item:selected{background:%10;color:%11;} "
        "QDockWidget{background:%4;} QDockWidget::title{padding:8px 10px;background:%4;border-bottom:1px solid %5;} "
        "QScrollArea{background:%4;border:0;} "
        "QScrollBar:vertical{background:%4;width:10px;margin:0;} QScrollBar::handle:vertical{background:%12;border-radius:5px;min-height:28px;} "
        "QScrollBar::handle:vertical:hover{background:%9;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} "
        "QScrollBar:horizontal{background:%4;height:10px;margin:0;} QScrollBar::handle:horizontal{background:%12;border-radius:5px;min-width:28px;} "
        "QScrollBar::handle:horizontal:hover{background:%9;} QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;} "
        "QTabBar::tab{background:%3;color:%2;padding:7px 3px;border-bottom:2px solid transparent;} QTabBar::tab:selected{color:%1;border-bottom-color:%9;} QTabBar::tab:hover{background:%7;} QTabBar:focus{border:1px solid %9;} "
        "QLineEdit#adjustment-search{margin:6px 8px;} QLabel#adjustment-search-empty{padding:8px;} "
        "QToolTip{background:%4;color:%1;border:1px solid %5;padding:4px 6px;} "
    ).arg(text.name(),muted.name(),shell.name(),panel.name(),line.name(),dark?QStringLiteral("#232327"):QStringLiteral("#ffffff"),hover.name(),pressed.name(),accent.name(),accentSoft.name(),selectedText.name(),edge.name(),family,dark?"dark":"light"));
}
QFrame *card(const QString &title,const QString &description,Glyph glyph,QFormLayout *&form,QWidget *parent){
    auto *frame=new QFrame(parent);frame->setObjectName("adjustment-card");auto *outer=new QVBoxLayout(frame);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    // Header text is the navigation/selector label, so it carries no expand state
    // suffix; only the arrow glyph and the tooltip change.
    auto *header=new QToolButton;header->setObjectName("card-header");header->setText(title);header->setIcon(icon(glyph));header->setIconSize({16,16});header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setArrowType(Qt::NoArrow);header->setIcon(icon(Glyph::ChevronDown));header->setCheckable(true);header->setChecked(true);
    header->setAutoRaise(true);header->setMinimumHeight(32);header->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    header->setAccessibleName(title);
    const auto describe=[description](bool open,bool missing){
        if(missing)return QString();
        if(description.isEmpty())return open?QStringLiteral("已展开"):QStringLiteral("已收起");
        return open?QStringLiteral("%1（已展开）").arg(description.left(240)):QStringLiteral("%1（已收起）").arg(description.left(240));
    };
    const bool missing=description.isEmpty();
    header->setToolTip(description);header->setAccessibleDescription(QStringLiteral("已展开。")+description);
    if(!missing)header->setAccessibleDescription(description);
    outer->addWidget(header);
    auto *body=new QWidget;body->setObjectName("card-body");auto *bodyLayout=new QVBoxLayout(body);bodyLayout->setContentsMargins(0,0,0,0);bodyLayout->setSpacing(6);
    // This empty widget keeps the documented indent next to the arrow glyph that
    // QToolButton reserves internally: the flat form rows stay aligned with the title.
    auto *indent=new QWidget;indent->setObjectName("card-body-indent");indent->setFixedWidth(10);indent->setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Preferred);
    // The description stays reachable as the tooltip/accessible description; the
    // permanent wrapped paragraph is gone so the rows get the vertical space.
    form=new QFormLayout;form->setContentsMargins(0,6,0,8);form->setVerticalSpacing(6);form->setHorizontalSpacing(8);form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);form->setLabelAlignment(Qt::AlignRight|Qt::AlignVCenter);form->setFormAlignment(Qt::AlignLeft|Qt::AlignTop);form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    auto *row=new QWidget;auto *rowLayout=new QHBoxLayout(row);rowLayout->setContentsMargins(0,0,0,0);rowLayout->setSpacing(0);rowLayout->addWidget(indent);rowLayout->addLayout(form,1);bodyLayout->addWidget(row);outer->addWidget(body);
    // Expansion only toggles the arrow and the description state reported to assistive
    // technology; card(), form and control object names keep their original values.
    QObject::connect(header,&QToolButton::toggled,body,[body,header,describe,missing](bool open){
        body->setVisible(open);
        header->setIcon(icon(open?Glyph::ChevronDown:Glyph::Next));
        header->setToolTip(describe(open,false));header->setAccessibleDescription(describe(open,false));
    });
    return frame;
}
// Bounded display-colour cache for the filmstrip. It only stores small converted
// thumbnails; the source icon, its pixels and every stored role stay untouched.
class FilmstripDisplayCache {
public:
    struct Entry{QIcon image;QSize size;qint64 key;};
    QByteArray profile;
    QList<Entry> entries;
    static constexpr int limit=64;
    void clear(){entries.clear();}
    QIcon converted(const QIcon &source,const QSize &size,qint64 key){
        for(auto it=entries.begin();it!=entries.end();++it)
            if(it->key==key&&it->size==size){const auto found=*it;entries.erase(it);entries.prepend(found);return found.image;}
        const auto pixels=source.pixmap(size);
        if(pixels.isNull()||!size.isValid())return source;
        const auto encoded=pixels.toImage();
        if(profile.isEmpty()&&(!encoded.colorSpace().isValid()||encoded.colorSpace()==QColorSpace(QColorSpace::SRgb)))return source;
        const auto result=transformDisplay(encoded,profile);
        // Fallback keeps the small source pixels unchanged; no managed-colour claim.
        const auto converted=result.error.isEmpty()&&!result.image.isNull()?QIcon(QPixmap::fromImage(result.image)):source;
        entries.prepend(Entry{converted,size,key});
        while(entries.size()>limit)entries.removeLast();
        return converted;
    }
};
class FilmstripDelegate final:public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &,const QModelIndex &) const override {return qobject_cast<QListWidget *>(parent())->gridSize();}
    void setTargetProfile(const QByteArray &monitorIcc){if(display.profile==monitorIcc)return;display.profile=monitorIcc;display.clear();}
    QByteArray currentProfile() const {return display.profile;}
    QIcon displayImage(const QIcon &source,const QSize &size) const {return display.converted(source,size,source.cacheKey());}
    void paint(QPainter *p,const QStyleOptionViewItem &o,const QModelIndex &index) const override {
        p->save();p->setRenderHint(QPainter::Antialiasing);const bool current=index.data(Qt::UserRole+1).toBool(),selected=o.state&QStyle::State_Selected;const int pick=index.data(Qt::UserRole+2).toInt(),cache=index.data(Qt::UserRole+3).toInt();
        QRectF card=o.rect.adjusted(4,4,-4,-4);auto bg=o.palette.color(QPalette::Base);if(selected)bg=o.palette.color(QPalette::AlternateBase);p->setBrush(bg);p->setPen(QPen(current?o.palette.color(QPalette::Highlight):selected?o.palette.color(QPalette::Highlight):o.palette.color(QPalette::Mid),current?2:1));p->drawRoundedRect(card,2,2);
        QRect imageRect=o.rect.adjusted(10,10,-10,-38);const auto image=index.data(Qt::DecorationRole).value<QIcon>();
        const auto pixels=displayImage(image,imageRect.size()).pixmap(imageRect.size());
        if(!pixels.isNull()){
            const QSizeF dimensions=QSizeF(pixels.size())/pixels.devicePixelRatio();
            const QRectF destination(QPointF(imageRect.center())-QPointF(dimensions.width()/2,dimensions.height()/2),dimensions);
            p->save();p->setRenderHint(QPainter::SmoothPixmapTransform);p->drawPixmap(destination,pixels,QRectF(QPointF(),pixels.size()));p->restore();
        }
        p->setPen(o.palette.color(QPalette::Text));const auto name=index.data(Qt::UserRole+4).toString();p->drawText(o.rect.adjusted(11,o.rect.height()-31,-11,-12),Qt::AlignLeft|Qt::AlignVCenter,o.fontMetrics.elidedText(name,Qt::ElideMiddle,o.rect.width()-36));
        p->setPen(Qt::NoPen);p->setBrush(cache==1?QColor("#91bba2"):cache==2?QColor("#d39d90"):o.palette.color(QPalette::Mid));p->drawEllipse(QPointF(o.rect.right()-15,o.rect.bottom()-12),2.5,2.5);
        if(pick){QRect badge(o.rect.right()-30,o.rect.top()+9,20,20);p->setBrush(o.palette.color(QPalette::Window));p->drawRoundedRect(badge,4,4);icon(pick==1?Glyph::Keep:Glyph::Reject).paint(p,badge.adjusted(2,2,-2,-2));}p->restore();
    }
private:
    mutable FilmstripDisplayCache display;
};
void installFilmstripDelegate(QListWidget *strip){strip->setItemDelegate(new FilmstripDelegate(strip));}
static FilmstripDelegate *filmstripDelegate(QListWidget *strip){return dynamic_cast<FilmstripDelegate *>(strip?strip->itemDelegate():nullptr);}
void setFilmstripDisplayProfile(QListWidget *strip,const QByteArray &monitorIcc){if(auto *delegate=filmstripDelegate(strip)){delegate->setTargetProfile(monitorIcc);strip->viewport()->update();}}
QIcon filmstripDisplayImage(QListWidget *strip,const QIcon &source,const QSize &size){auto *delegate=filmstripDelegate(strip);return delegate?delegate->displayImage(source,size):source;}
}
