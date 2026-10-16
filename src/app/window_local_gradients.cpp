#include "window.h"
#include "local_adjustment_panel.h"
#include "local_gradient_controls.h"
#include "render/source_coordinate_map.h"
#include <QTabWidget>
#include <cmath>
#include <limits>

void Window::setLocalGradientTool(int kind){
    endControlEdit();
    localGradientKind=kind==1||kind==2?kind:0;selectedLocalComponent.clear();
    localPanel->setBrushActive(false);refreshLocalPanel();syncCanvasTools();
}
void Window::selectLocalComponent(const QString &id){
    const QString copy=id;endControlEdit();selectedLocalComponent.clear();
    const auto doc=currentPhotoDocument();
    if(doc)for(const auto &layer:doc->layers)if(layer.id==selectedLocalLayer)
        for(const auto &component:layer.mask.components)if(component.id==copy){
            selectedLocalComponent=copy;
            localGradientKind=std::holds_alternative<LocalEdit::LinearGeometry>(component.geometry)?1:
                std::holds_alternative<LocalEdit::RadialGeometry>(component.geometry)?2:0;
            localPanel->setBrushActive(false);break;
        }
    refreshLocalPanel();syncCanvasTools();
}
void Window::syncLocalGradientHandles(){
    QVector<QPointF> handles,guide;
    const auto publish=[&]{canvas->setGradientHandles(std::move(handles));canvas->setGradientGuide(std::move(guide));};
    if(!localGradientKind||!canvas->brushMode()||!adjustmentScope||adjustmentScope->currentIndex()!=1||!localEditingAvailable()){publish();return;}
    const auto doc=currentPhotoDocument();if(!doc){publish();return;}
    const LocalEdit::MaskComponent *selected=nullptr;
    for(const auto &layer:doc->layers)if(layer.id==selectedLocalLayer)
        for(const auto &component:layer.mask.components)if(component.id==selectedLocalComponent){selected=&component;break;}
    if(!selected){publish();return;}
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const auto project=[&](QPointF source){
        QPointF uv(nan,nan);sourceToOutput(source,doc->asset.normalizedSize,doc->global,uv);return uv;
    };
    for(const auto &source:LocalEdit::gradientHandlePoints(selected->geometry))handles.append(project(source));
    if(const auto *linear=std::get_if<LocalEdit::LinearGeometry>(&selected->geometry)){
        for(int i=0;i<=64;++i)guide.append(project(linear->start+(linear->end-linear->start)*(double(i)/64)));
    }else if(const auto *radial=std::get_if<LocalEdit::RadialGeometry>(&selected->geometry)){
        const double radians=radial->angleDegrees*std::acos(-1.)/180,co=std::cos(radians),si=std::sin(radians);
        for(int i=0;i<=128;++i){
            const double phase=2*std::acos(-1.)*i/128,x=radial->radiusX*std::cos(phase),y=radial->radiusY*std::sin(phase);
            guide.append(project(radial->center+QPointF(co*x-si*y,si*x+co*y)));
        }
        const auto points=LocalEdit::gradientHandlePoints(selected->geometry);
        guide.append({nan,nan});guide.append(project(points[0]));guide.append(project(points[1]));
        guide.append({nan,nan});guide.append(project(points[0]));guide.append(project(points[2]));
    }
    publish();
}
