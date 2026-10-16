#include "local_layer_operations.h"
#include <QHash>
#include <QSet>
#include <cmath>
#include <limits>

namespace LocalEdit {
DocumentSnapshot changedLayers(const PhotoDocument &base,const LayerChange &change,QString &error){
    error.clear();if(!validDocument(base,error,true))return {};
    const auto fail=[&](const QString &message)->DocumentSnapshot{error=message;return {};};
    auto result=base;
    if(change.kind==LayerChangeKind::Reorder){
        if(change.order.size()!=base.layers.size())return fail("Layer order is not a complete permutation");
        QHash<QString,AdjustmentLayer> layers;QStringList original;
        for(const auto &layer:base.layers){layers.insert(layer.id,layer);original.append(layer.id);}
        QSet<QString> seen;QVector<AdjustmentLayer> ordered;ordered.reserve(base.layers.size());
        for(const auto &id:change.order){
            if(!layers.contains(id)||seen.contains(id))return fail("Layer order has unknown or duplicate ids");
            seen.insert(id);ordered.append(layers.value(id));
        }
        if(change.order==original)return {};
        result.layers=std::move(ordered);
    }else{
        qsizetype index=-1;
        for(qsizetype i=0;i<result.layers.size();++i)if(result.layers[i].id==change.layerId){index=i;break;}
        if(index<0)return fail("Local layer no longer exists");
        auto &layer=result.layers[index];
        const auto bump=[&]{if(layer.version==std::numeric_limits<quint64>::max()){error="Local layer version overflow";return false;}++layer.version;return true;};
        switch(change.kind){
        case LayerChangeKind::Rename:{
            const auto name=change.name.trimmed();if(name.isEmpty())return fail("Local layer name cannot be empty");
            if(name==layer.name)return {};
            if(!bump())return {};layer.name=name;break;
        }
        case LayerChangeKind::Visibility:
            if(layer.enabled==change.visible)return {};
            if(!bump())return {};layer.enabled=change.visible;break;
        case LayerChangeKind::Opacity:
            if(!std::isfinite(change.opacity)||change.opacity<0||change.opacity>1)return fail("Local layer opacity is outside [0,1]");
            if(layer.opacity==change.opacity)return {};
            if(!bump())return {};layer.opacity=change.opacity;break;
        case LayerChangeKind::Duplicate:{
            if(base.layers.size()>=maximumLayers)return fail("Maximum local layer count reached");
            if(change.componentIds.size()!=layer.mask.components.size())return fail("Duplicate component ids are incomplete");
            auto copy=layer;copy.id=change.duplicateId;copy.name=change.name.trimmed();
            if(copy.name.isEmpty())return fail("Local layer name cannot be empty");
            copy.version=1;copy.mask.version=1;
            for(qsizetype i=0;i<copy.mask.components.size();++i)copy.mask.components[i].id=change.componentIds[i];
            result.layers.insert(index+1,std::move(copy));break;
        }
        case LayerChangeKind::Remove:result.layers.removeAt(index);break;
        case LayerChangeKind::RemoveComponent:{
            qsizetype component=-1;
            for(qsizetype i=0;i<layer.mask.components.size();++i)
                if(layer.mask.components[i].id==change.componentId){component=i;break;}
            if(component<0)return fail("Mask component no longer exists");
            if(layer.mask.version==std::numeric_limits<quint64>::max())return fail("Mask version overflow");
            if(!bump())return {};
            ++layer.mask.version;layer.mask.components.removeAt(component);break;
        }
        default:return fail("Unsupported layer operation");
        }
    }
    if(!validDocument(result,error,true))return {};
    return std::make_shared<const PhotoDocument>(std::move(result));
}
}
