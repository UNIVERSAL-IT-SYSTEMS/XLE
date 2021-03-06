// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "HighlightEffects.h"
#include "../../SceneEngine/SceneEngineUtils.h"
#include "../../RenderCore/Metal/DeviceContext.h"
#include "../../RenderCore/Metal/Shader.h"
#include "../../RenderCore/Metal/InputLayout.h"
#include "../../RenderCore/Metal/ShaderResource.h"
#include "../../RenderCore/Techniques/CommonResources.h"
#include "../../RenderCore/Techniques/ResourceBox.h"
#include "../../BufferUploads/IBufferUploads.h"
#include "../../BufferUploads/ResourceLocator.h"
#include "../../Assets/AssetUtils.h"
#include "../../Utility/StringFormat.h"

namespace ToolsRig
{
    using namespace RenderCore;

    const UInt4 HighlightByStencilSettings::NoHighlight = UInt4(0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff);

    HighlightByStencilSettings::HighlightByStencilSettings()
    {
        _outlineColor = Float3(1.5f, 1.35f, .7f);

        _highlightedMarker = NoHighlight;
        for (unsigned c=0; c<dimof(_stencilToMarkerMap); ++c) 
            _stencilToMarkerMap[c] = NoHighlight;
    }

    void ExecuteHighlightByStencil(
        Metal::DeviceContext& metalContext,
        Metal::ShaderResourceView& inputStencil,
        const HighlightByStencilSettings& settings,
        bool onlyHighlighted)
    {
        metalContext.BindPS(MakeResourceList(Metal::ConstantBuffer(&settings, sizeof(settings))));
        metalContext.BindPS(MakeResourceList(inputStencil));
        metalContext.Bind(Techniques::CommonResources()._dssDisable);
        metalContext.Bind(Techniques::CommonResources()._blendAlphaPremultiplied);
        metalContext.Bind(Metal::Topology::TriangleStrip);
        metalContext.Unbind<Metal::BoundInputLayout>();

        auto desc = BufferUploads::ExtractDesc(*inputStencil.GetResource());
        if (desc._type != BufferUploads::BufferDesc::Type::Texture) return;

        bool stencilInput = 
            Metal::AsTypelessFormat((Metal::NativeFormat::Enum)desc._textureDesc._nativePixelFormat) 
            == Metal::NativeFormat::R24G8_TYPELESS;
                
        StringMeld<64, ::Assets::ResChar> params;
        params << "ONLY_HIGHLIGHTED=" << unsigned(onlyHighlighted);
        params << ";INPUT_MODE=" << (stencilInput?0:1);

        {
            auto& shader = ::Assets::GetAssetDep<Metal::ShaderProgram>(
                "game/xleres/basic2D.vsh:fullscreen:vs_*", 
                "game/xleres/Vis/HighlightVis.psh:HighlightByStencil:ps_*",
                (const ::Assets::ResChar*)params);
                
            metalContext.Bind(shader);
            metalContext.Draw(4);
        }

        {
            auto& shader = ::Assets::GetAssetDep<Metal::ShaderProgram>(
                "game/xleres/basic2D.vsh:fullscreen:vs_*", 
                "game/xleres/Vis/HighlightVis.psh:OutlineByStencil:ps_*",
                (const ::Assets::ResChar*)params);
                
            metalContext.Bind(shader);
            metalContext.Draw(4);
        }

        metalContext.UnbindPS<Metal::ShaderResourceView>(0, 1);
    }


///////////////////////////////////////////////////////////////////////////////////////////////////

    class CommonOffscreenTarget
    {
    public:
        class Desc
        {
        public:
            unsigned _width, _height;
            Metal::NativeFormat::Enum _format;
            Desc(unsigned width, unsigned height, Metal::NativeFormat::Enum format)
                : _width(width), _height(height), _format(format) {}
        };

        Metal::RenderTargetView _rtv;
        Metal::ShaderResourceView _srv;
        intrusive_ptr<BufferUploads::ResourceLocator> _resource;

        CommonOffscreenTarget(const Desc& desc);
        ~CommonOffscreenTarget();
    };

    CommonOffscreenTarget::CommonOffscreenTarget(const Desc& desc)
    {
            //  Still some work involved to just create a texture
            //  
        using namespace BufferUploads;
        auto bufferDesc = CreateDesc(
            BindFlag::ShaderResource|BindFlag::RenderTarget, 0, GPUAccess::Write,
            TextureDesc::Plain2D(desc._width, desc._height, desc._format),
            "CommonOffscreen");

        auto resource = SceneEngine::GetBufferUploads().Transaction_Immediate(bufferDesc);

        Metal::RenderTargetView rtv(resource->GetUnderlying());
        Metal::ShaderResourceView srv(resource->GetUnderlying());

        _rtv = std::move(rtv);
        _srv = std::move(srv);
        _resource = std::move(resource);
    }

    CommonOffscreenTarget::~CommonOffscreenTarget() {}

    class HighlightShaders
    {
    public:
        class Desc {};

        const Metal::ShaderProgram* _drawHighlight;
        Metal::BoundUniforms _drawHighlightUniforms;

        const Metal::ShaderProgram* _drawShadow;
        Metal::BoundUniforms _drawShadowUniforms;

        const std::shared_ptr<::Assets::DependencyValidation>& GetDependencyValidation() const { return _validationCallback; }

        HighlightShaders(const Desc&);
    protected:
        std::shared_ptr<::Assets::DependencyValidation>  _validationCallback;
    };

    HighlightShaders::HighlightShaders(const Desc&)
    {
        //// ////
        _drawHighlight = &::Assets::GetAssetDep<Metal::ShaderProgram>(
            "game/xleres/basic2D.vsh:fullscreen:vs_*", 
            "game/xleres/effects/outlinehighlight.psh:main:ps_*");
        _drawHighlightUniforms = Metal::BoundUniforms(*_drawHighlight);
        _drawHighlightUniforms.BindConstantBuffer(Hash64("$Globals"), 0, 1);

        //// ////
        _drawShadow = &::Assets::GetAssetDep<Metal::ShaderProgram>(
            "game/xleres/basic2D.vsh:fullscreen:vs_*", 
            "game/xleres/effects/outlinehighlight.psh:main_shadow:ps_*");
        _drawShadowUniforms = Metal::BoundUniforms(*_drawShadow);
        _drawShadowUniforms.BindConstantBuffer(Hash64("ShadowHighlightSettings"), 0, 1);

        //// ////
        _validationCallback = std::make_shared<::Assets::DependencyValidation>();
        ::Assets::RegisterAssetDependency(_validationCallback, _drawHighlight->GetDependencyValidation());
    }

    class BinaryHighlight::Pimpl
    {
    public:
        SceneEngine::SavedTargets _savedTargets;
        Metal::ShaderResourceView _srv;

        Pimpl(Metal::DeviceContext& metalContext)
            : _savedTargets(metalContext)
        {}
        ~Pimpl() {}
    };

    BinaryHighlight::BinaryHighlight(Metal::DeviceContext& metalContext)
    {
        _pimpl = std::make_unique<Pimpl>(metalContext);

        const auto& viewport = _pimpl->_savedTargets.GetViewports()[0];

        auto& offscreen = Techniques::FindCachedBox<CommonOffscreenTarget>(
            CommonOffscreenTarget::Desc(unsigned(viewport.Width), unsigned(viewport.Height), 
            Metal::NativeFormat::R8G8B8A8_UNORM));

        const bool doDepthTest = true;
        if (constant_expression<doDepthTest>::result()) {
            metalContext.Bind(Techniques::CommonResources()._dssReadOnly);
            Metal::DepthStencilView dsv(_pimpl->_savedTargets.GetDepthStencilView());
            metalContext.Bind(MakeResourceList(offscreen._rtv), &dsv);
        } else {
            metalContext.Bind(MakeResourceList(offscreen._rtv), nullptr);
        }

        metalContext.Clear(offscreen._rtv, Float4(0.f, 0.f, 0.f, 0.f));

        _pimpl->_srv = offscreen._srv;
    }

    void BinaryHighlight::FinishWithOutlineAndOverlay(Metal::DeviceContext& metalContext, Float3 outlineColor, unsigned overlayColor)
    {
        static Float3 highlightColO(1.5f, 1.35f, .7f);
        static unsigned overlayColO = 1;

        outlineColor = highlightColO;
        overlayColor = overlayColO;

        _pimpl->_savedTargets.ResetToOldTargets(metalContext);

        HighlightByStencilSettings settings;
        settings._outlineColor = outlineColor;
        for (unsigned c=1; c<dimof(settings._stencilToMarkerMap); ++c) settings._stencilToMarkerMap[c] = UInt4(overlayColor, overlayColor, overlayColor, overlayColor);

        ExecuteHighlightByStencil(metalContext, _pimpl->_srv, settings, false);
    }

    void BinaryHighlight::FinishWithOutline(Metal::DeviceContext& metalContext, Float3 outlineColor)
    {
        _pimpl->_savedTargets.ResetToOldTargets(metalContext);

            //  now we can render these objects over the main image, 
            //  using some filtering

        metalContext.BindPS(MakeResourceList(_pimpl->_srv));

        struct Constants
        {
            Float3 _color; unsigned _dummy;
        } constants = { outlineColor, 0 };
        SharedPkt pkts[] = { MakeSharedPkt(constants) };

        auto& shaders = Techniques::FindCachedBoxDep<HighlightShaders>(HighlightShaders::Desc());
        shaders._drawHighlightUniforms.Apply(
            metalContext, 
            Metal::UniformsStream(), 
            Metal::UniformsStream(pkts, nullptr, dimof(pkts)));
        metalContext.Bind(*shaders._drawHighlight);
        metalContext.Bind(Techniques::CommonResources()._blendAlphaPremultiplied);
        metalContext.Bind(Techniques::CommonResources()._dssDisable);
        metalContext.Bind(Metal::Topology::TriangleStrip);
        metalContext.Draw(4);
    }

    void BinaryHighlight::FinishWithShadow(Metal::DeviceContext& metalContext, Float4 shadowColor)
    {
        _pimpl->_savedTargets.ResetToOldTargets(metalContext);

            //  now we can render these objects over the main image, 
            //  using some filtering

        metalContext.BindPS(MakeResourceList(_pimpl->_srv));

        struct Constants
        {
            Float4 _shadowColor;
        } constants = { shadowColor };
        SharedPkt pkts[] = { MakeSharedPkt(constants) };

        auto& shaders = Techniques::FindCachedBoxDep<HighlightShaders>(HighlightShaders::Desc());
        shaders._drawShadowUniforms.Apply(
            metalContext, 
            Metal::UniformsStream(), 
            Metal::UniformsStream(pkts, nullptr, dimof(pkts)));
        metalContext.Bind(*shaders._drawShadow);
        metalContext.Bind(Techniques::CommonResources()._blendStraightAlpha);
        metalContext.Bind(Techniques::CommonResources()._dssDisable);
        metalContext.Bind(Metal::Topology::TriangleStrip);
        metalContext.Draw(4);
    }

    BinaryHighlight::~BinaryHighlight() {}

}

