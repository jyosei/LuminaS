#include "adjustment_navigation.h"
#include <QComboBox>
#include <QDockWidget>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QAbstractButton>
#include <QTabBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QKeyEvent>
#include <QMenu>
#include <QAction>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>

namespace Ui {
class AdjustmentSearch final:public QLineEdit {
public:
    using QLineEdit::QLineEdit;
protected:
    bool event(QEvent *event) override {
        if(event->type()==QEvent::ShortcutOverride&&static_cast<QKeyEvent *>(event)->key()==Qt::Key_Escape){event->accept();return true;}
        return QLineEdit::event(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if(event->key()==Qt::Key_Escape){clear();event->accept();return;}QLineEdit::keyPressEvent(event);
    }
};
void installAdjustmentNavigation(QDockWidget *dock,QScrollArea *scroll){
    if(!dock||!scroll||dock->widget()!=scroll||!scroll->widget())return;
    QWidget *content=scroll->widget();
    if(!content->layout())return;
    QList<QPointer<QToolButton>> headers;
    QList<QPointer<QWidget>> cards;
    // Layout order is the presentation order; QObject child order need not be.
    for(int i=0;i<content->layout()->count();++i){
        QWidget *card=content->layout()->itemAt(i)->widget();
        if(card&&card->objectName()==QStringLiteral("adjustment-card"))
            if(auto *header=card->findChild<QToolButton *>(QStringLiteral("card-header"))){
                headers.append(header);cards.append(card);
            }
    }
    if(headers.isEmpty())return;
    auto *wrapper=new QWidget;
    wrapper->setObjectName(QStringLiteral("adjustment-navigation"));
    auto *outer=new QVBoxLayout(wrapper);
    outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    auto *groups=new QTabBar(wrapper);groups->setObjectName("adjustment-groups");
    groups->setAccessibleName(QStringLiteral("调整分组"));groups->setExpanding(true);groups->setFocusPolicy(Qt::StrongFocus);
    for(const auto &title:{"全部","基础","色彩","细节","光学","风格"})groups->addTab(QString::fromUtf8(title));
    outer->addWidget(groups);
    auto *search=new AdjustmentSearch(wrapper);search->setObjectName("adjustment-search");
    search->setAccessibleName(QStringLiteral("搜索调整"));search->setPlaceholderText(QStringLiteral("搜索参数，例如：曝光、锐化"));
    search->setClearButtonEnabled(true);auto *searchRow=new QWidget(wrapper);auto *searchLine=new QHBoxLayout(searchRow);searchLine->setContentsMargins(0,0,8,0);searchLine->setSpacing(0);searchLine->addWidget(search,1);
    auto *options=new QToolButton(searchRow);options->setText(QStringLiteral("选项"));options->setObjectName("adjustment-options");options->setAccessibleName(QStringLiteral("调整面板选项"));options->setPopupMode(QToolButton::InstantPopup);
    auto *menu=new QMenu(options);auto *jump=menu->addMenu(QStringLiteral("跳转到调整"));options->setMenu(menu);searchLine->addWidget(options);outer->addWidget(searchRow);
    auto *empty=new QLabel(QStringLiteral("未找到调整项，修改关键词或按 Esc 清除。"),wrapper);
    empty->setObjectName("adjustment-search-empty");empty->setWordWrap(true);empty->hide();outer->addWidget(empty);
    auto *toolbar=new QFrame(wrapper);
    toolbar->setObjectName(QStringLiteral("adjustment-navigation-toolbar"));
    auto *line=new QHBoxLayout(toolbar);line->setContentsMargins(8,4,8,4);
    auto *choice=new QComboBox(toolbar);
    choice->setObjectName(QStringLiteral("adjustment-section-choice"));
    choice->setAccessibleName(QStringLiteral("调整分类"));
    choice->setToolTip(QStringLiteral("跳转并展开调整卡片"));
    choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    choice->setMinimumContentsLength(8);
    choice->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
    for(const auto &header:headers)choice->addItem(header->icon(),header->text());
    line->addWidget(choice);outer->addWidget(toolbar);
    auto *expand=new QPushButton(QStringLiteral("展开"),toolbar);
    expand->setObjectName(QStringLiteral("adjustment-expand-all"));
    auto *collapse=new QPushButton(QStringLiteral("收起"),toolbar);
    collapse->setObjectName(QStringLiteral("adjustment-collapse-all"));
    line->addWidget(expand);line->addWidget(collapse);
    scroll->setParent(wrapper);outer->addWidget(scroll,1);dock->setWidget(wrapper);

    // Keep the card nearly flush with the viewport top; reuses the panel's existing
    // small inner padding rather than introducing a new inset.
    const int kTopPadding=8;
    QPointer<QScrollArea> area(scroll);QPointer<QWidget> panel(content);
    auto generation=std::make_shared<quint64>(0);
    struct SearchState {bool active=false;QList<bool> expanded;};
    auto state=std::make_shared<SearchState>();
    QList<int> categories;QStringList searchable;
    for(int i=0;i<headers.size();++i){
        const auto title=headers[i]->text();int category=0;
        if(title==QStringLiteral("白平衡")||title==QStringLiteral("光线")||title==QStringLiteral("色彩")||title==QStringLiteral("质感"))category=1;
        else if(title.contains(QStringLiteral("曲线"))||title.contains(QStringLiteral("分色"))||title.contains(QStringLiteral("分级"))||title.contains(QStringLiteral("黑白"))||title.contains(QStringLiteral("原色")))category=2;
        else if(title==QStringLiteral("细节"))category=3;
        else if(title.contains(QStringLiteral("色差"))||title.contains(QStringLiteral("紫绿"))||title.contains(QStringLiteral("畸变"))||title.contains(QStringLiteral("镜头"))||title==QStringLiteral("透视")||title==QStringLiteral("构图"))category=4;
        else if(title.contains(QStringLiteral("预设"))||title==QStringLiteral("颗粒")||title==QStringLiteral("裁剪后暗角"))category=5;
        categories<<category;QString words=title+" "+cards[i]->toolTip();
        for(auto *label:cards[i]->findChildren<QLabel *>())words+=" "+label->text()+" "+label->toolTip();
        for(auto *button:cards[i]->findChildren<QAbstractButton *>())words+=" "+button->text()+" "+button->accessibleName();
        searchable<<words;
    }
    auto filter=[headers,cards,categories,searchable,groups,search,empty,choice,state,generation,area,panel,wrapper]{
        const auto ticket=++*generation;const auto query=search->text().trimmed();const bool searching=!query.isEmpty();
        if(searching&&!state->active){state->expanded.clear();for(const auto &h:headers)state->expanded<<(h&&h->isChecked());}
        if(!searching&&state->active)for(int i=0;i<headers.size();++i)if(headers[i])headers[i]->setChecked(state->expanded.value(i));
        state->active=searching;int count=0,firstVisible=-1;
        for(int i=0;i<cards.size();++i){const bool visible=searching?searchable[i].contains(query,Qt::CaseInsensitive):(groups->currentIndex()==0||categories[i]==groups->currentIndex());
            if(cards[i])cards[i]->setVisible(visible);if(visible){++count;if(firstVisible<0)firstVisible=i;if(searching&&headers[i])headers[i]->setChecked(true);}}
        if(choice->currentIndex()<0||choice->currentIndex()>=cards.size()||!cards[choice->currentIndex()]||cards[choice->currentIndex()]->isHidden()){
            QSignalBlocker blocked(choice);choice->setCurrentIndex(firstVisible);
        }
        empty->setVisible(count==0);
        QTimer::singleShot(0,wrapper,[area,panel,generation,ticket]{if(ticket!=*generation||!area||!panel)return;if(panel->layout())panel->layout()->activate();area->verticalScrollBar()->setValue(0);});
    };
    QObject::connect(groups,&QTabBar::currentChanged,wrapper,[filter](int){filter();});
    QObject::connect(search,&QLineEdit::textChanged,wrapper,[filter](const QString &){filter();});
    auto select=[headers,cards,area,panel,wrapper,generation,search,groups,filter](int index,bool focus){
        if(index<0||index>=headers.size()||!headers[index])return;
        {QSignalBlocker a(search),b(groups);search->clear();groups->setCurrentIndex(0);}filter();
        QPointer<QToolButton> target=headers[index];
        QPointer<QWidget> card=cards.value(index);
        target->setChecked(true);
        const auto ticket=++*generation;
        // Expansion posts layout work; scroll after that work, and discard old jumps.
        QTimer::singleShot(0,wrapper,[area,panel,target,card,generation,ticket,focus]{
            if(ticket!=*generation||!area||!panel||!target||!card)return;
            if(panel->layout())panel->layout()->activate();
            // Align the target CARD to the viewport top. ensureWidgetVisible on the
            // header would only reveal the header at the bottom edge, pushing the
            // parameter rows offscreen. Position is content-relative; the scroll bar
            // clamps naturally, so trailing cards may stop short of the top while
            // every reachable row stays visible.
            if(auto *bar=area->verticalScrollBar()){
                const int top=card->mapTo(panel,QPoint(0,0)).y();
                bar->setValue(top-kTopPadding);
            }
            if(focus&&target->isEnabled())target->setFocus(Qt::OtherFocusReason);
        });
    };
    QObject::connect(choice,&QComboBox::currentIndexChanged,wrapper,[select](int i){select(i,false);});
    // Keep focus in the selector so repeated arrow keys remain usable.
    QObject::connect(choice,&QComboBox::activated,wrapper,[select](int i){select(i,false);});
    expand->setToolTip(QStringLiteral("展开当前分组中的调整"));collapse->setToolTip(QStringLiteral("收起当前分组中的调整"));
    QObject::connect(expand,&QPushButton::clicked,wrapper,[headers,cards,generation]{++*generation;for(int i=0;i<headers.size();++i)if(headers[i]&&cards[i]&&!cards[i]->isHidden())headers[i]->setChecked(true);});
    QObject::connect(collapse,&QPushButton::clicked,wrapper,[headers,cards,generation]{++*generation;for(int i=0;i<headers.size();++i)if(headers[i]&&cards[i]&&!cards[i]->isHidden())headers[i]->setChecked(false);});
    for(int i=0;i<headers.size();++i){auto *action=jump->addAction(headers[i]->text());QObject::connect(action,&QAction::triggered,wrapper,[select,i]{select(i,false);});}
    auto *expandAction=menu->addAction(QStringLiteral("展开当前分组"));QObject::connect(expandAction,&QAction::triggered,expand,&QPushButton::click);
    auto *collapseAction=menu->addAction(QStringLiteral("收起当前分组"));QObject::connect(collapseAction,&QAction::triggered,collapse,&QPushButton::click);
    if(dock->property("compactAdjustmentNavigation").toBool())toolbar->hide();
    const auto initial=dock->property("initialAdjustmentGroup");if(initial.isValid()){
        groups->setCurrentIndex(qBound(0,initial.toInt(),5));
        for(int i=0;i<headers.size();++i)if(categories[i]==1)headers[i]->setChecked(headers[i]->text()==QStringLiteral("光线"));
    }
}
}
