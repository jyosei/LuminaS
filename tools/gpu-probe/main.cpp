// Isolated G0/G1 reference experiment. No application backend selection changes.
#include "render/cpu_renderer.h"
#include "color/color_pipeline.h"
#include "render/local_reference.h"
#include "render/local_mask.h"
#include <QThreadPool>
#include <QtConcurrentRun>
#include <atomic>
#include <QApplication>
#include <QRhiWidget>
#include <QPainter>
#include <QTimer>
#include <QEventLoop>
#include <QScopeGuard>
#include <QVBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <rhi/qshader.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include <functional>
#include <type_traits>

namespace {
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
double elapsed(QElapsedTimer &timer){return double(timer.nsecsElapsed())/1e6;}
QJsonObject distribution(std::vector<double> values){
    if(values.empty())return {};
    std::sort(values.begin(),values.end());
    return {{"count",int(values.size())},{"medianMs",values[values.size()/2]},
            {"p95Ms",values[size_t(std::ceil(values.size()*.95))-1]},
            {"minMs",values.front()},{"maxMs",values.back()}};
}
QJsonObject memory(QRhi *rhi){
    PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
    GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),sizeof(counters));
    QJsonObject result{{"workingSetBytes",double(counters.WorkingSetSize)},
        {"privateCommitBytes",double(counters.PrivateUsage)},
        {"peakWorkingSetBytes",double(counters.PeakWorkingSetSize)},
        {"peakCommitBytes",double(counters.PeakPagefileUsage)}};
    if(!rhi)return result;
    const auto *handles=static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
    auto *device=static_cast<ID3D11Device *>(handles->dev);
    IDXGIDevice *dxgiDevice=nullptr;IDXGIAdapter *adapter=nullptr;IDXGIAdapter3 *adapter3=nullptr;
    if(SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void **>(&dxgiDevice)))){
        if(SUCCEEDED(dxgiDevice->GetAdapter(&adapter))){
            DXGI_ADAPTER_DESC desc{};
            if(SUCCEEDED(adapter->GetDesc(&desc))){
                result.insert("dedicatedVideoBytes",double(desc.DedicatedVideoMemory));
                result.insert("dedicatedSystemBytes",double(desc.DedicatedSystemMemory));
                result.insert("sharedSystemBytes",double(desc.SharedSystemMemory));
            }
            if(SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3),reinterpret_cast<void **>(&adapter3)))){
                for(const auto group:{DXGI_MEMORY_SEGMENT_GROUP_LOCAL,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL}){
                    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
                    if(SUCCEEDED(adapter3->QueryVideoMemoryInfo(0,group,&info)))
                        result.insert(group==DXGI_MEMORY_SEGMENT_GROUP_LOCAL?"dxgiLocal":"dxgiNonLocal",
                            QJsonObject{{"currentUsageBytes",double(info.CurrentUsage)},{"budgetBytes",double(info.Budget)}});
                }
                adapter3->Release();
            }
            adapter->Release();
        }
        dxgiDevice->Release();
    }
    return result;
}
QJsonObject compare(const QImage &reference,const QByteArray &actual){
    require(actual.size()==reference.sizeInBytes(),"Unexpected float readback byte count");
    // QByteArray alignment is not a float-pointer contract; memcpy each pixel.
    quint64 failures=0;double maxRgb=0,maxAlpha=0,maxRelativeTolerance=0;
    for(int y=0;y<reference.height();++y){
        auto *row=reinterpret_cast<const float *>(reference.constScanLine(y));
        for(int x=0;x<reference.width();++x){
            float gpu[4];std::memcpy(gpu,actual.constData()+(qint64(y)*reference.width()+x)*16,16);
            for(int c=0;c<4;++c){
                const double ref=row[x*4+c],difference=std::abs(double(gpu[c])-ref);
                const double tolerance=c==3?1e-6:1e-5+1e-4*std::abs(ref);
                if(!std::isfinite(gpu[c])||difference>tolerance)++failures;
                if(c==3)maxAlpha=std::max(maxAlpha,difference);
                else {maxRgb=std::max(maxRgb,difference);maxRelativeTolerance=std::max(maxRelativeTolerance,difference/tolerance);}
            }
        }
    }
    return {{"passed",failures==0},{"failedChannels",double(failures)},
        {"maxRgbAbsoluteError",maxRgb},{"maxAlphaAbsoluteError",maxAlpha},
        {"maxRgbErrorOverTolerance",maxRelativeTolerance}};
}
SourceFrame synthetic(QSize size){
    SourceFrame frame;frame.originalSize=size;frame.linear=QImage(size,QImage::Format_RGBA32FPx4);
    require(!frame.linear.isNull(),"Synthetic frame allocation failed");
    for(int y=0;y<size.height();++y){
        auto *row=reinterpret_cast<float *>(frame.linear.scanLine(y));
        for(int x=0;x<size.width();++x){
            const quint32 n=quint32(x)*1664525u+quint32(y)*1013904223u;
            row[x*4]=float(n&65535)/65535*9-1;
            row[x*4+1]=float((n>>8)&65535)/65535*6-.5f;
            row[x*4+2]=float((n>>16)&65535)/65535*3-.25f;
            row[x*4+3]=x%31==0?0.f:x%17==0?1e-9f:x%13==0?.25f:1.f;
        }
    }
    return frame;
}
struct Pipeline {
    QRhi *rhi;
    std::unique_ptr<QRhiTexture> input,output;
    std::unique_ptr<QRhiSampler> sampler;
    std::unique_ptr<QRhiBuffer> uniform;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiComputePipeline> pipeline;
    explicit Pipeline(QRhi *r,QSize size):rhi(r){
        input.reset(rhi->newTexture(QRhiTexture::RGBA32F,size));
        output.reset(rhi->newTexture(QRhiTexture::RGBA32F,size,1,QRhiTexture::UsedWithLoadStore|QRhiTexture::UsedAsTransferSource));
        sampler.reset(rhi->newSampler(QRhiSampler::Nearest,QRhiSampler::Nearest,QRhiSampler::None,QRhiSampler::ClampToEdge,QRhiSampler::ClampToEdge));
        uniform.reset(rhi->newBuffer(QRhiBuffer::Dynamic,QRhiBuffer::UniformBuffer,32));
        require(input->create()&&output->create()&&sampler->create()&&uniform->create(),"GPU resource allocation failed");
        bindings.reset(rhi->newShaderResourceBindings());
        bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0,QRhiShaderResourceBinding::ComputeStage,uniform.get()),
            QRhiShaderResourceBinding::sampledTexture(1,QRhiShaderResourceBinding::ComputeStage,input.get(),sampler.get()),
            QRhiShaderResourceBinding::imageStore(2,QRhiShaderResourceBinding::ComputeStage,output.get(),0)});
        require(bindings->create(),"GPU bindings creation failed");
        QFile file(":/gpu/tools/gpu-probe/basic.comp.qsb");require(file.open(QIODevice::ReadOnly),"Cannot read compiled shader");
        const auto shader=QShader::fromSerialized(file.readAll());require(shader.isValid(),"Invalid compiled shader");
        pipeline.reset(rhi->newComputePipeline());
        pipeline->setShaderStage({QRhiShaderStage::Compute,shader});pipeline->setShaderResourceBindings(bindings.get());
        require(pipeline->create(),"Compute pipeline creation failed");
    }
    void record(QRhiCommandBuffer *cb,const QImage &source,const Recipe &recipe,bool upload){
        auto *updates=rhi->nextResourceUpdateBatch();
        const float params[]{float(std::exp2(recipe.warmth)),float(std::exp2(recipe.tint)),float(std::exp2(-recipe.warmth)),float(std::exp2(recipe.exposure)),float(std::exp2(recipe.contrast)),float(recipe.saturation),0,0};
        updates->updateDynamicBuffer(uniform.get(),0,sizeof(params),params);
        if(upload){
            QRhiTextureSubresourceUploadDescription desc(QByteArray(reinterpret_cast<const char *>(source.constBits()),source.sizeInBytes()));
            desc.setDataStride(source.bytesPerLine());
            updates->uploadTexture(input.get(),QRhiTextureUploadDescription({QRhiTextureUploadEntry(0,0,desc)}));
        }
        cb->beginComputePass(updates);cb->setComputePipeline(pipeline.get());cb->setShaderResources();
        cb->dispatch((source.width()+15)/16,(source.height()+15)/16,1);cb->endComputePass();
    }
    QJsonObject dispatch(const QImage &source,const Recipe &recipe,bool upload,bool readback,QByteArray *pixels=nullptr){
        QElapsedTimer timer;timer.start();QRhiCommandBuffer *cb=nullptr;
        require(rhi->beginOffscreenFrame(&cb)==QRhi::FrameOpSuccess,"beginOffscreenFrame failed");
        record(cb,source,recipe,upload);
        QRhiReadbackResult result;bool completed=false;
        if(readback){
            result.completed=[&]{completed=true;};
            auto *readUpdates=rhi->nextResourceUpdateBatch();readUpdates->readBackTexture(QRhiReadbackDescription(output.get()),&result);cb->resourceUpdate(readUpdates);
        }
        const double submit=elapsed(timer);
        require(rhi->endOffscreenFrame()==QRhi::FrameOpSuccess,"endOffscreenFrame failed");
        require(rhi->finish()==QRhi::FrameOpSuccess,"GPU finish failed");
        const double total=elapsed(timer);
        if(readback){require(completed,"Readback callback missing");if(pixels)*pixels=std::move(result.data);}
        return {{"totalMs",total},{"cpuPrepareAndCommandEnqueueMs",submit},
            {"synchronizeAndReadbackMs",total-submit},{"gpuTimestampMs",cb->lastCompletedGpuTime()*1000}};
    }
};
#include "presentation.inc"
#include "local.inc"
}

int main(int argc,char **argv){
    QApplication app(argc,argv);QCommandLineParser parser;
    parser.addOptions({QCommandLineOption("report","New JSON report path","path"),QCommandLineOption("iterations","Warm measured iterations (100 minimum)","count","100"),
        QCommandLineOption("size","small, 4k, 24mp or 45mp","size","4k"),QCommandLineOption("local","G3 resident local parameters; no presentation"),QCommandLineOption("capabilities-only","Only check GPU support"),QCommandLineOption("present","G2 native widget experiment, fixed sRGB only")});
    parser.process(app);const auto reportPath=parser.value("report");
    if(reportPath.isEmpty()||QFile::exists(reportPath)){qCritical("A new --report path is required");return 2;}
    QJsonObject report{{"schema",1},{"qtVersion",qVersion()},{"timestampUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {"scope","G0/G1 basic float32 only; no display or S3 implementation"},{"productionBackendChanged",false}};
    int exitCode=0;
    try{
        require(!(parser.isSet("present")&&parser.isSet("local")),"Choose only one experiment mode");
        if(parser.isSet("present")){
            bool ok=false;const int iterations=parser.value("iterations").toInt(&ok);
            require(ok&&iterations>=100&&iterations<=1000,"Iterations must be 100..1000");
            require(parser.value("size")=="4k","Native experiment currently requires 4k input");
            report.insert("presentation",runPresentation(iterations,QFileInfo(reportPath).absolutePath()+"/native-synthetic.png"));report.insert("passed",true);
            report.insert("scope","G2 native fixed sRGB reference; excludes arbitrary monitor ICC and physical scanout");
        }else{
        QElapsedTimer init;init.start();QRhiD3D11InitParams params;
        std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::D3D11,&params,QRhi::EnableTimestamps));
        require(bool(rhi),"D3D11 QRhi unavailable; production CPU remains available");
        const auto info=rhi->driverInfo();const auto *native=static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
        report.insert("device",QJsonObject{{"name",QString::fromUtf8(info.deviceName)},{"vendorId",double(info.vendorId)},
            {"deviceId",double(info.deviceId)},{"deviceType",int(info.deviceType)},
            {"featureLevel",int(static_cast<ID3D11Device *>(native->dev)->GetFeatureLevel())},
            {"compute",rhi->isFeatureSupported(QRhi::Compute)},{"rgba32fSample",rhi->isTextureFormatSupported(QRhiTexture::RGBA32F)},
            {"rgba32fStorage",rhi->isTextureFormatSupported(QRhiTexture::RGBA32F,QRhiTexture::UsedWithLoadStore)},
            {"timestamps",rhi->isFeatureSupported(QRhi::Timestamps)},{"maxTextureSize",rhi->resourceLimit(QRhi::TextureSizeMax)}});
        report.insert("initializationMs",elapsed(init));report.insert("memoryInitial",memory(rhi.get()));
        require(info.deviceType!=QRhiDriverInfo::CpuDevice,"Software adapter is not hardware GPU evidence");
        require(rhi->isFeatureSupported(QRhi::Compute)&&rhi->isTextureFormatSupported(QRhiTexture::RGBA32F,QRhiTexture::UsedWithLoadStore),"Required float32 compute support missing");
        if(!parser.isSet("capabilities-only")){
            bool ok=false;int iterations=parser.value("iterations").toInt(&ok);require(ok&&iterations>=100&&iterations<=1000,"Iterations must be 100..1000");
            if(parser.isSet("local")){
                require(parser.value("size")=="small"||parser.value("size")=="4k","Local experiment accepts only small or 4k");
                report.insert("local",runLocalExperiment(rhi.get(),parser.value("size")=="small"?QSize(257,131):QSize(3840,2560),iterations));
                report.insert("memoryAfterLocalResourcesReleased",memory(rhi.get()));
                rhi->releaseCachedResources();
                require(rhi->finish()==QRhi::FrameOpSuccess,"Local cache release finish failed");
                report.insert("memoryAfterLocalCacheTrim",memory(rhi.get()));
                report.insert("scope","G3 one resident local layer; CPU prefix/mask, no native presentation");
            }else{
            const auto sizeName=parser.value("size");QSize size;
            if(sizeName=="small")size={257,131};else if(sizeName=="4k")size={3840,2560};else if(sizeName=="24mp")size={6000,4000};else if(sizeName=="45mp")size={9000,5000};
            require(size.isValid(),"Invalid size selection");auto source=synthetic(size);
            require(size.width()<=rhi->resourceLimit(QRhi::TextureSizeMax)&&size.height()<=rhi->resourceLimit(QRhi::TextureSizeMax),"Frame exceeds texture limit");
            init.restart();Pipeline pipeline(rhi.get(),size);report.insert("pipelineInitializationMs",elapsed(init));
            report.insert("width",size.width());report.insert("height",size.height());
            report.insert("inputBytes",double(source.linear.sizeInBytes()));
            report.insert("inFlightPolicy","one serial offscreen frame, two float32 textures; CPU reference and readback explicitly live");
            QJsonArray correctness;Recipe recipe;bool allPassed=true;
            for(int test=0;test<4;++test){
                recipe={};if(test==1){recipe.exposure=2.25;recipe.saturation=1.5;recipe.warmth=.3;recipe.tint=-.2;}
                if(test==2){recipe.exposure=-4;recipe.saturation=0;recipe.contrast=.7;recipe.warmth=-1;recipe.tint=.5;}
                if(test==3){recipe.exposure=4;recipe.saturation=2;recipe.contrast=-.7;recipe.warmth=1;recipe.tint=-1;}
                QString error;auto cpu=renderLinear(source,recipe,0,error);require(error.isEmpty()&&!cpu.isNull(),"CPU reference failed");
                QByteArray actual;auto stages=pipeline.dispatch(source.linear,recipe,true,true,&actual);
                auto result=compare(cpu,actual);allPassed&=result.value("passed").toBool();result.insert("recipe",recipeJson(recipe));result.insert("stages",stages);correctness.append(result);
            }
            report.insert("correctness",correctness);require(allPassed,"CPU/GPU tolerance check failed; do not benchmark incorrect shader");
            recipe={};recipe.exposure=.75;recipe.saturation=1.25;recipe.warmth=.15;recipe.tint=-.1;
            for(int i=0;i<5;++i){QString error;require(!renderLinear(source,recipe,0,error).isNull(),"CPU warmup failed");pipeline.dispatch(source.linear,recipe,true,true);}
            std::vector<double> cpuTimes,roundtripTimes,residentTimes,prepareTimes,syncTimes,gpuTimes;
            for(int i=0;i<iterations;++i){
                // Alternate order to reduce systematic warm/thermal ordering bias.
                auto cpu=[&]{QElapsedTimer timer;timer.start();QString error;auto result=renderLinear(source,recipe,0,error);require(!result.isNull()&&error.isEmpty(),"CPU measurement failed");cpuTimes.push_back(elapsed(timer));};
                auto gpu=[&]{auto sample=pipeline.dispatch(source.linear,recipe,true,true);roundtripTimes.push_back(sample.value("totalMs").toDouble());prepareTimes.push_back(sample.value("cpuPrepareAndCommandEnqueueMs").toDouble());syncTimes.push_back(sample.value("synchronizeAndReadbackMs").toDouble());};
                if(i%2){cpu();gpu();}else{gpu();cpu();}
            }
            // A separate resident-compute ceiling; this is explicitly NOT a display measurement.
            for(int i=0;i<iterations;++i){auto changing=recipe;changing.exposure+=double(i%11)/100;auto sample=pipeline.dispatch(source.linear,changing,false,false);residentTimes.push_back(sample.value("totalMs").toDouble());const double gpuMs=sample.value("gpuTimestampMs").toDouble();if(gpuMs>0)gpuTimes.push_back(gpuMs);}
            report.insert("cpuFullLinear",distribution(cpuTimes));report.insert("gpuRoundtrip",distribution(roundtripTimes));report.insert("gpuResidentComputeOnly",distribution(residentTimes));
            report.insert("cpuPrepareUploadEnqueue",distribution(prepareTimes));report.insert("gpuRoundtripSyncAndReadback",distribution(syncTimes));report.insert("gpuResidentTimestamp",distribution(gpuTimes));
            report.insert("memoryFinal",memory(rhi.get()));
            report.insert("roundtripMedianReduction",1-distribution(roundtripTimes).value("medianMs").toDouble()/distribution(cpuTimes).value("medianMs").toDouble());
            report.insert("measurementLimitations",QJsonArray{"upload/command times are CPU enqueue, not isolated GPU DMA", "synchronization/readback grouped; timestamps only where supported", "resident compute excludes presentation, ICC and input decode", "synthetic input; no cross-vendor certification", "GPU statistics describe DXGI segments, not dedicated/shared physical attribution on UMA"});
            }
        }
        if(parser.isSet("local")){
            rhi.reset();report.insert("memoryAfterLocalDeviceReleased",memory(nullptr));
        }
        report.insert("passed",true);
        }
    }catch(const std::exception &error){report.insert("passed",false);report.insert("error",QString::fromUtf8(error.what()));exitCode=1;}
    QSaveFile file(reportPath);if(!file.open(QIODevice::WriteOnly)||file.write(QJsonDocument(report).toJson())<0||!file.commit()){qCritical("Cannot write report");return 2;}
    qInfo().noquote()<<QJsonDocument(report).toJson(QJsonDocument::Compact);return exitCode;
}
