// Isolated G3: all resources belong to this offscreen QRhi. No app/cache handles.
struct LocalPipeline {
    QRhi *rhi;
    std::unique_ptr<QRhiTexture> input,output,coverage,gain;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiBuffer> uniform;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiComputePipeline> pipeline;
    int sourceUploads=0,maskUploads=0;
    LocalPipeline(QRhi *r,QSize size):rhi(r){
        require(rhi->isTextureFormatSupported(QRhiTexture::R32F),"R32F unsupported");
        input.reset(rhi->newTexture(QRhiTexture::RGBA32F,size));
        output.reset(rhi->newTexture(QRhiTexture::RGBA32F,size,1,QRhiTexture::UsedWithLoadStore|QRhiTexture::UsedAsTransferSource));
        coverage.reset(rhi->newTexture(QRhiTexture::R32F,size));gain.reset(rhi->newTexture(QRhiTexture::R32F,{4097,1}));
        sampler.reset(rhi->newSampler(QRhiSampler::Nearest,QRhiSampler::Nearest,QRhiSampler::None,QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));
        uniform.reset(rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,48));
        require(input->create()&&output->create()&&coverage->create()&&gain->create()&&sampler->create()&&uniform->create(),"Local GPU allocation failed");
        bindings.reset(rhi->newShaderResourceBindings());bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0,QRhiShaderResourceBinding::ComputeStage,uniform.get()),
            QRhiShaderResourceBinding::sampledTexture(1,QRhiShaderResourceBinding::ComputeStage,input.get(),sampler.get()),
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
            put(input.get(),reinterpret_cast<const char *>(source.constBits()),source.sizeInBytes(),source.bytesPerLine());
            put(coverage.get(),reinterpret_cast<const char *>(mask.constData()),mask.size()*sizeof(float),source.width()*sizeof(float));
            ++sourceUploads;++maskUploads;
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
LocalEdit::Mask residentMask(QSize size){
    using namespace LocalEdit;Mask mask;MaskComponent component;component.id="resident-radial";
    component.geometry=RadialGeometry{{size.width()*.5,size.height()*.5},size.width()*.3,size.height()*.3,23};
    mask.components={component};mask.feather=2.3;return mask;
}
LocalEdit::LocalParameters nativeLocalParameters(const Recipe &recipe){
    return {recipe.exposure,.2,-.3,.4,.2,-.2,.15,-.1,recipe.saturation};
}
LocalEdit::PhotoDocument nativeLocalDocument(const SourceFrame &s,const Recipe &recipe){
    using namespace LocalEdit;PhotoDocument d;d.asset.id="resident-asset";d.asset.sourcePath=s.path;
    d.asset.sourceHash=s.sha256;d.asset.normalizedSize=s.originalSize;d.asset.raw=s.raw;
    AdjustmentLayer layer;layer.id="resident-layer";layer.mask=residentMask(s.linear.size());
    layer.opacity=.65;layer.parameters=nativeLocalParameters(recipe);d.layers={layer};return d;
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
