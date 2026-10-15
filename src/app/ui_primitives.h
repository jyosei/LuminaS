#pragma once
#include <QIcon>
#include <QByteArray>
#include <QSize>
#include <QFrame>
#include <QFont>
class QFormLayout;
class QWidget;
class QListWidget;
namespace Ui {
enum class Glyph { Import, Folder, Browse, Edit, Undo, Redo, Previous, Next, Fit, Hand, Crop, Compare, Keep, Reject, Unmarked, Filmstrip, Theme, Reset, Light, Color, Curve, Balance, ChevronDown };
QIcon icon(Glyph glyph);
QFont brandFont(int pixels=24);
QIcon brandIcon();
void applyTheme(QWidget *window,bool dark);
QFrame *card(const QString &title,const QString &description,Glyph glyph,QFormLayout *&form,QWidget *parent=nullptr);
void installFilmstripDelegate(QListWidget *strip);
// Display-only filmstrip plumbing: the caller supplies one validated effective monitor
// profile; this layer only routes it through transformDisplay for small thumbnails.
void setFilmstripDisplayProfile(QListWidget *strip,const QByteArray &monitorIcc);
QIcon filmstripDisplayImage(QListWidget *strip,const QIcon &source,const QSize &size);
}
