// Isolated G3: all resources belong to one supplied QRhi. No app/cache handles.
struct LocalPipeline {
    QRhi *rhi;
    std::unique_ptr<QRhiTexture> input,output,coverage,gain;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiBuffer> uniform;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiComputePipeline> pipeline;
    int sourceUploads=0,maskUploads=0;
    LocalPipeline(QRhi *r,QSize size,QRhiTexture *previous=nullptr):rhi(r){
        require(rhi->isTextureFormatSupported(QRhiTexture::R32F),"R32F unsupported");
        if(!previous)input.reset(rhi->newTexture(QRhiTexture::RGBA32F,size));
        output.reset(rhi->newTexture(QRhiTexture::RGBA32F,size,1,QRhiTexture::UsedWithLoadStore|QRhiTexture::UsedAsTransferSource));
        coverage.reset(rhi->newTexture(QRhiTexture::R32F,size));gain.reset(rhi->newTexture(QRhiTexture::R32F,{4097,1}));
        sampler.reset(rhi->newSampler(QRhiSampler::Nearest,QRhiSampler::Nearest,QRhiSampler::None,QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));
        uniform.reset(rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,48));
        require((!input||input->create())&&output->create()&&coverage->create()&&gain->create()&&sampler->create()&&uniform->create(),"Local GPU allocation failed");
        bindings.reset(rhi->newShaderResourceBindings());bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0,QRhiShaderResourceBinding::ComputeStage,uniform.get()),
            QRhiShaderResourceBinding::sampledTexture(1,QRhiShaderResourceBinding::ComputeStage,previous?previous:input.get(),sampler.get()),
            QRhiShaderResourceBinding::imageStore(2,QRhiShaderResourceBinding::ComputeStage,output.get(),0),
            QRhiShaderResourceBinding::sampledTexture(3,QRhiShaderResourceBinding::ComputeStage,coverage.get(),sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(4,QRhiShaderResourceBinding::ComputeStage,gain.get(),sampler.get())});
        require(bindings->create(),"Local GPU bindings failed");
        QFile file(":/gpu/tools/gpu-probe/local.comp.qsb");require(file.open(QIODevice::ReadOnly),"Local shader missing");
        const auto shader=QShader::fromSerialized(file.readAll());require(shader.isValid(),"Local shader invalid");
        pipeline.reset(rhi->newComputePipeline());pipeline->setShaderStage({QRhiShaderStage::Compute,shader});pipeline->setShaderResourceBindings(bindings.get());
        require(pipeline->create(),"Local GPU pipeline failed");
    }
    void record(QRhiCommandBuffer *cb,const QImage &source,const QVector<float> &mask,
        const LocalEdit::LocalParameters &p,float opacity,bool upload){
        require(mask.size()==qsizetype(source.width())*source.height(),"Local mask size mismatch");
        auto *updates=rhi->nextResourceUpdateBatch();
        const bool tonal=p.highlights!=0||p.shadows!=0||p.whites!=0||p.blacks!=0;
        const float values[]{float(std::exp2(p.warmth)),float(std::exp2(p.tint)),float(std::exp2(-p.warmth)),float(std::exp2(p.exposure)),
            float(std::exp2(p.contrast)),float(p.saturation),opacity,p==LocalEdit::LocalParameters{}?1.f:0.f,tonal?1.f:0.f,0,0,0};
        updates->updateDynamicBuffer(uniform.get(),0,sizeof(values),values);
        std::array<float,4097> table{};
        if(tonal)for(size_t i=0;i<table.size();++i){
            const double y=double(i)*4/(table.size()-1),t=y/(y+.18),dark=1-t;
            table[i]=float(std::exp2(2*p.shadows*dark*dark+2*p.highlights*std::pow(t,4)+p.blacks*std::pow(dark,12)+p.whites*std::pow(t,12)));
        }
        const auto put=[&](QRhiTexture *texture,const char *data,qsizetype bytes,int stride){
            QRhiTextureSubresourceUploadDescription desc(QByteArray(data,bytes));desc.setDataStride(stride);
            updates->uploadTexture(texture,QRhiTextureUploadDescription({QRhiTextureUploadEntry(0,0,desc)}));
        };
        put(gain.get(),reinterpret_cast<const char *>(table.data()),sizeof(table),sizeof(table));
        if(upload){
            if(input){put(input.get(),reinterpret_cast<const char *>(source.constBits()),source.sizeInBytes(),source.bytesPerLine());++sourceUploads;}
            put(coverage.get(),reinterpret_cast<const char *>(mask.constData()),mask.size()*sizeof(float),source.width()*sizeof(float));
            ++maskUploads;
        }
        cb->beginComputePass(updates);cb->setComputePipeline(pipeline.get());cb->setShaderResources();
        cb->dispatch((source.width()+15)/16,(source.height()+15)/16,1);cb->endComputePass();
    }
    QJsonObject dispatch(const QImage &source,const QVector<float> &mask,const LocalEdit::LocalParameters &p,
        float opacity,bool upload,bool readback,QByteArray *pixels=nullptr){
        QElapsedTimer timer;timer.start();QRhiCommandBuffer *cb=nullptr;
        require(rhi->beginOffscreenFrame(&cb)==QRhi::FrameOpSuccess,"Local begin failed");
        record(cb,source,mask,p,opacity,upload);
        QRhiReadbackResult result;bool completed=false;
        if(readback){result.completed=[&]{completed=true;};auto *batch=rhi->nextResourceUpdateBatch();
            batch->readBackTexture(QRhiReadbackDescription(output.get()),&result);cb->resourceUpdate(batch);}
        const auto enqueued=elapsed(timer);
        require(rhi->endOffscreenFrame()==QRhi::FrameOpSuccess,"Local end failed");
        require(rhi->finish()==QRhi::FrameOpSuccess,"Local finish failed");
        if(readback){require(completed,"Local readback missing");if(pixels)*pixels=std::move(result.data);}
        return {{"totalMs",elapsed(timer)},{"enqueueMs",enqueued},{"gpuTimestampMs",cb->lastCompletedGpuTime()*1000}};
    }
};
LocalEdit::Mask residentMask(QSize size,int index=0){
    using namespace LocalEdit;Mask mask;MaskComponent component;component.id="resident-radial";
    component.geometry=RadialGeometry{{size.width()*.5,size.height()*.5},size.width()*.3,size.height()*.3,23};
    if(index==1){component.id="resident-linear";component.geometry=LinearGeometry{{size.width()*.1,size.height()*.2},{size.width()*.85,size.height()*.7}};mask.invert=true;}
    mask.components={component};mask.feather=index==0?2.3:1.2;return mask;
}
LocalEdit::LocalParameters nativeLocalParameters(const Recipe &recipe,int index=0){
    if(index==1)return {-.3,-.2,.2,-.1,-.15,.1,-.1,.08,1.1};
    return {recipe.exposure,.2,-.3,.4,.2,-.2,.15,-.1,recipe.saturation};
}
LocalEdit::PhotoDocument nativeLocalDocument(const SourceFrame &s,const Recipe &recipe,int layers=1){
    using namespace LocalEdit;PhotoDocument d;d.asset.id="resident-asset";d.asset.sourcePath=s.path;
    d.asset.sourceHash=s.sha256;d.asset.normalizedSize=s.originalSize;d.asset.raw=s.raw;
    AdjustmentLayer layer;layer.id="resident-layer";layer.mask=residentMask(s.linear.size());
    layer.opacity=.65;layer.parameters=nativeLocalParameters(recipe);d.layers={layer};
    if(layers==2){layer.id="resident-second";layer.mask=residentMask(s.linear.size(),1);layer.opacity=.55;layer.parameters=nativeLocalParameters(recipe,1);d.layers.append(layer);}
    return d;
}
QJsonObject validateLocalStack(){
    using namespace LocalEdit;
    QRhiD3D11InitParams params;std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::D3D11,&params));
    require(bool(rhi)&&rhi->driverInfo().deviceType!=QRhiDriverInfo::CpuDevice,"Stack validation needs hardware QRhi");
    const auto s=synthetic({257,131});QVector<float> masks[2];QString error;
    for(int i=0;i<2;++i){const PreparedBrushMask mask(residentMask(s.linear.size(),i));
        require(rasterizeMaskPreview(mask,{s.originalSize,s.linear.size(),{}},s.linear,masks[i],error),"Stack coverage preparation failed");}
    LocalPipeline first(rhi.get(),s.linear.size()),second(rhi.get(),s.linear.size(),first.output.get());
    Recipe recipe;recipe.exposure=.4;recipe.saturation=1.25;QJsonArray checks;QByteArray forwardPixels;QImage forwardReference;
    for(int test=0;test<4;++test){
        const int order[2]{test==1?1:0,test==1?0:1};const float opacities[2]{test==2?0.f:.65f,test==3?0.f:.55f};
        QRhiCommandBuffer *cb=nullptr;require(rhi->beginOffscreenFrame(&cb)==QRhi::FrameOpSuccess,"Stack begin failed");
        first.record(cb,s.linear,masks[order[0]],nativeLocalParameters(recipe,order[0]),opacities[order[0]],true);
        second.record(cb,s.linear,masks[order[1]],nativeLocalParameters(recipe,order[1]),opacities[order[1]],true);
        QRhiReadbackResult result;bool completed=false;result.completed=[&]{completed=true;};
        auto *batch=rhi->nextResourceUpdateBatch();batch->readBackTexture(QRhiReadbackDescription(second.output.get()),&result);cb->resourceUpdate(batch);
        require(rhi->endOffscreenFrame()==QRhi::FrameOpSuccess&&rhi->finish()==QRhi::FrameOpSuccess&&completed,"Stack readback failed");
        auto expected=s.linear.copy();
        for(int layer=0;layer<2;++layer){const int index=order[layer];const LocalPixelTransform transform(nativeLocalParameters(recipe,index));
            for(int y=0;y<expected.height();++y){auto *row=reinterpret_cast<float *>(expected.scanLine(y));
                for(int x=0;x<expected.width();++x)require(transform.apply(row+x*4,masks[index][y*expected.width()+x],opacities[index]),"Stack reference rejected");}}
        auto check=compare(expected,result.data);check.insert("case",test);checks.append(check);
        if(!check.value("passed").toBool())qWarning().noquote()<<QJsonDocument(check).toJson(QJsonDocument::Compact);
        require(check.value("passed").toBool(),"Stack numerical mismatch");
        if(test==0){forwardPixels=result.data;forwardReference=expected;}
        if(test==1)require(result.data!=forwardPixels&&expected!=forwardReference,"Stack order failed to change output");
    }
    return {{"passed",true},{"checks",checks},{"size","257x131"},{"layers",2}};
}
QJsonObject runLocalExperiment(QRhi *rhi,QSize size,int iterations){
    using namespace LocalEdit;
    auto s=synthetic(size);const auto mask=residentMask(size);
    const PreparedBrushMask prepared(mask);QString error;QVector<float> coverage;
    require(rasterizeMaskPreview(prepared,{size,size,{}},s.linear,coverage,error),"CPU mask preparation failed");
    LocalPipeline pipeline(rhi,size);LocalParameters mixed{.7,.2,-.3,.4,.2,-.2,.15,-.1,.75};
    const auto reference=[&](const LocalParameters &p,float opacity,bool parallel=false){
        QImage image=s.linear.copy();require(!image.isNull(),"Local CPU copy failed");const LocalPixelTransform transform(p);
        auto *bits=image.bits();const auto stride=image.bytesPerLine();std::atomic<bool> failed{false};
        const auto rows=[&](int begin,int end){
            for(int y=begin;y<end;++y){auto *row=reinterpret_cast<float *>(bits+y*stride);
                for(int x=0;x<size.width();++x)if(coverage[y*size.width()+x]>0)
                    if(!transform.apply(row+4*x,coverage[y*size.width()+x],opacity))failed=true;}
        };
        if(parallel){
            static QThreadPool pool;pool.setMaxThreadCount(4);QFuture<void> jobs[4];
            for(int part=0;part<4;++part)jobs[part]=QtConcurrent::run(&pool,[&,part]{rows(size.height()*part/4,size.height()*(part+1)/4);});
            for(auto &job:jobs)job.waitForFinished();
        }else rows(0,size.height());
        require(!failed,"Local CPU rejected");
        return image;
    };
    QJsonArray checks;bool first=true;
    for(int test=0;test<5;++test){
        auto p=mixed;float opacity=.65f;
        if(test==0)p={};if(test==2){p.exposure=-3;p.saturation=0;}if(test==3)opacity=0;
        if(test==4){p.exposure=3;p.highlights=1;p.shadows=-1;p.whites=1;p.blacks=-1;opacity=1;}
        QByteArray bytes;const auto stages=pipeline.dispatch(s.linear,coverage,p,opacity,first,true,&bytes);first=false;
        const auto expected=reference(p,opacity);require(expected==reference(p,opacity,true),"Parallel local CPU mismatch");
        auto check=compare(expected,bytes);check.insert("stages",stages);check.insert("case",test);checks.append(check);
        if(!check.value("passed").toBool())qWarning().noquote()<<QJsonDocument(check).toJson(QJsonDocument::Compact);
        require(check.value("passed").toBool(),"Local GPU numerical mismatch");
    }
    for(int i=0;i<5;++i){pipeline.dispatch(s.linear,coverage,mixed,.65f,false,false);reference(mixed,.65f,true);}
    std::vector<double> resident,cpu;for(int i=0;i<iterations;++i){
        auto p=mixed;p.exposure+=double(i%11)*.01;
        const auto gpu=[&]{resident.push_back(pipeline.dispatch(s.linear,coverage,p,.65f,false,false).value("totalMs").toDouble());};
        const auto host=[&]{QElapsedTimer timer;timer.start();const auto output=reference(p,.65f,true);cpu.push_back(elapsed(timer));};
        if(i%2){gpu();host();}else{host();gpu();}
    }
    return {{"passed",true},{"checks",checks},{"width",size.width()},{"height",size.height()},
        {"cpuCopyAndLocalParallel4",distribution(cpu)},{"gpuResidentLocalComputeOnly",distribution(resident)},
        {"sourceUploads",pipeline.sourceUploads},{"maskUploads",pipeline.maskUploads},{"memory",memory(rhi)},
        {"limitations",QJsonArray{"synthetic one-layer G3 only; isolated four-worker CPU comparison excludes cache lookup/global stages", "CPU mask prepared once; source/mask uploaded once", "resident time includes parameter/LUT upload and GPU synchronization; excludes display, ICC and mask changes", "CPU baseline includes full float copy and four-worker local transform; no claimed end-to-end speedup"}}};
}
