#ifndef CALF_PLATFORM_WINDOWS_D3D11_HPP_
#define CALF_PLATFORM_WINDOWS_D3D11_HPP_

#include "com.hpp"
#include "window.hpp"

#include <d3d11_1.h>
#include <wrl.h>
#include <d3dcompiler.h>
#include <directxmath.h>

#include <memory>
#include <string>

namespace calf {
namespace platform {
namespace windows {
namespace d3d11 {

class device;
class context;

using Microsoft::WRL::ComPtr;
class context {
public:
  context(ID3D11DeviceContext* ctx) : ctx_(ctx) {}
  ~context() = default;

  operator ID3D11DeviceContext*() { return ctx_.Get(); }
  ID3D11DeviceContext* operator->() { return ctx_.Get(); }

private:
  ComPtr<ID3D11DeviceContext> ctx_;
};

template< typename T >
class scoped_binder {
public:
  scoped_binder(const std::shared_ptr<context>& ctx,
      const std::shared_ptr<T>& target) : target_(target) {
    if (target_) {
      target_->bind(ctx);
    }
  }
  ~scoped_binder() {
    if (target_) {
      target_->unbind();
    }
  }

private:
  const std::shared_ptr<T> target_;
};

class swapchain {
public:
  swapchain(int width, int height, IDXGIDevice* device, IDXGISwapChain1* swapchain, 
            ID3D11RenderTargetView* rtv, 
            ID3D11SamplerState* sampler, 
            ID3D11BlendState* blender)
    : dev_(device),
      width_(width),
      height_(height),
      swapchain_(swapchain), 
      rtv_(rtv), 
      sampler_(sampler),
      blender_(blender) {}

  void bind(const std::shared_ptr<context>& ctx) {
    ctx_ = ctx;

    // 设置上下文
    (*ctx)->OMSetRenderTargets(
        1, rtv_.GetAddressOf(), nullptr);
    float factor[] = {0.0f, 0.0f, 0.0f, 0.0f};
    (*ctx)->OMSetBlendState(blender_.Get(), factor, 0xFFFFFFFF);
    (*ctx)->PSSetSamplers(0, 1, sampler_.GetAddressOf());
  }

  void unbind() {
    ctx_.reset();
  }

  void resize(int width, int height) {
    if (width <= 0 || height <= 0 || width == width_ || height == height_) {
      return;
    }
    width_ = width;
    height_ = height;

    auto &ctx = *ctx_.lock();

    ctx->OMSetRenderTargets(0, 0, 0);

    DXGI_SWAP_CHAIN_DESC desc;
    swapchain_->GetDesc(&desc);
    auto hr = swapchain_->ResizeBuffers(0, width, height, desc.BufferDesc.Format,
                                        desc.Flags);
    if (FAILED(hr)) {
      return;
    }

    ComPtr<ID3D11Texture2D> buffer;
    hr = swapchain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)buffer.GetAddressOf());
    if (FAILED(hr)) {
      return;
    }

    ComPtr<ID3D11Device> dev;
    ctx->GetDevice(dev.GetAddressOf());
    if (dev) {
      D3D11_RENDER_TARGET_VIEW_DESC vdesc = {};
      vdesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
      vdesc.Texture2D.MipSlice = 0;
      vdesc.Format = desc.BufferDesc.Format;

      hr = dev->CreateRenderTargetView(buffer.Get(), &vdesc, rtv_.ReleaseAndGetAddressOf());
      if (SUCCEEDED(hr)) {
        ctx->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
      }
    }

    D3D11_VIEWPORT vp;
    vp.Width = static_cast<float>(width);
    vp.Height = static_cast<float>(height);
    vp.MinDepth = D3D11_MIN_DEPTH;
    vp.MaxDepth = D3D11_MAX_DEPTH;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    ctx->RSSetViewports(1, &vp);
  }

  // 清理目标视图
  void clear(float red, float green, float blue, float alpha) {
    auto ctx = ctx_.lock();

    if (!!ctx) {
      FLOAT colors[] = {red, green, blue, alpha};
      (*ctx)->ClearRenderTargetView(rtv_.Get(), colors);
    }
  }

  void present(int sync_interval) {
    swapchain_->Present(sync_interval, 0);
  }

  void present(int sync_interval, std::vector<win32_rect> &dirty_rects) {
    RECT rc;
    std::vector<RECT> rc_vec;
    for (auto &drc : dirty_rects) {
      rc.left = drc.x;
      rc.top = drc.y;
      rc.right = drc.x + drc.w;
      rc.bottom = drc.y + drc.h;
      rc_vec.push_back(rc);
    }
    DXGI_PRESENT_PARAMETERS params;
    if (first_frame_) {
      params.DirtyRectsCount = 0;
      params.pDirtyRects = nullptr;
      first_frame_ = false;
    } else {
      params.DirtyRectsCount = rc_vec.size();
      params.pDirtyRects = &rc_vec[0];
    }
    params.pScrollOffset = nullptr;
    params.pScrollRect = nullptr;
    HRESULT hr = swapchain_->Present1(sync_interval, 0, &params);
  }

  void present(int sync_interval, RECT &dirty_rect) {
    DXGI_PRESENT_PARAMETERS params;
    if (first_frame_) {
      params.DirtyRectsCount = 0;
      params.pDirtyRects = nullptr;
      first_frame_ = false;
    } else {
      params.DirtyRectsCount = 1;
      params.pDirtyRects = &dirty_rect;
    }
    params.pScrollOffset = nullptr;
    params.pScrollRect = nullptr;
    HRESULT hr = swapchain_->Present1(sync_interval, 0, &params);
  }

  IDXGISurface* surface() {
    ComPtr<IDXGISurface> sf;
    HRESULT hr = swapchain_->GetBuffer(0, __uuidof(IDXGISurface), 
        reinterpret_cast<void**>(sf.GetAddressOf()));
    if (SUCCEEDED(hr)) {
      return sf.Detach();
    }
    return nullptr;
  }

  IDXGIDevice* dxgi_device() { return dev_.Get(); }
  IDXGISwapChain1* swapchain1() { return swapchain_.Get();  }

private:
  int width_;
  int height_;

  ComPtr<IDXGIDevice> dev_;
  ComPtr<IDXGISwapChain1> swapchain_;
  ComPtr<ID3D11RenderTargetView> rtv_;
  ComPtr<ID3D11SamplerState> sampler_;
  ComPtr<ID3D11BlendState> blender_;

  std::weak_ptr<context> ctx_;
  bool first_frame_ = true;
};

template< UINT t_bind_flags, UINT t_misc_flags >
struct texture2d_traits {
  static UINT bind_flags() { return t_bind_flags; }
  static UINT misc_flags() { return t_misc_flags; }
};

using render_target_texture2d_traits = 
    texture2d_traits< D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE, 0>;
using shared_texture2d_traits = 
    texture2d_traits< D3D11_BIND_SHADER_RESOURCE, D3D11_RESOURCE_MISC_SHARED>;

class texture2d {
public:
  texture2d() {}
  texture2d(ID3D11Texture2D* tex, ID3D11ShaderResourceView* srv)
    : tex_(tex), 
      srv_(srv) {}

  void bind(const std::shared_ptr<context>& ctx) {
    ctx_ = ctx;

    (*ctx)->PSSetShaderResources(0, 1, srv_.GetAddressOf());
  }

  void unbind() {
    ctx_.reset();
  }

  void resize(const std::shared_ptr<context>& ctx, int width, int height) {
    ComPtr<ID3D11Device> dev;
    (*ctx)->GetDevice(dev.GetAddressOf());
    D3D11_TEXTURE2D_DESC desc;
    tex_->GetDesc(&desc);
    desc.Width = width;
    desc.Height = height;
    HRESULT hr = dev->CreateTexture2D(&desc, nullptr, tex_.ReleaseAndGetAddressOf());

    if (FAILED(hr)) {
      return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    srv_desc.Format = desc.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    hr = dev->CreateShaderResourceView(tex_.Get(), &srv_desc, 
        srv_.GetAddressOf());
    if (FAILED(hr)) {
      return;
    }
  }

  void copy_from(const std::shared_ptr<texture2d>& other) {
    auto &ctx = *ctx_.lock();

    //D3D11_BOX box;
    //box.left = 0;
    //box.top = 0;
    //box.front = 0;
    //box.right = rc.w;
    //box.bottom =  rc.h;
    //box.back = 1;
    //(*ctx)->CopySubresourceRegion(it->second->texture(), 
    //    ::D3D11CalcSubresource(0, D3D11_TEXTURECUBE_FACE_POSITIVE_X, 1), 
    //    0, 0, 0, pair.second->texture(), 0, &box);
    ctx->CopyResource(tex_.Get(), other->texture());
  }

  void reset(const std::shared_ptr<texture2d>& other) {
    tex_ = other->texture();
  }

  IDXGISurface* surface() {
    ComPtr<IDXGISurface> sf;
    HRESULT hr = tex_.As(&sf);
    if (SUCCEEDED(hr)) {
      return sf.Detach();
    }
    return nullptr;
  }

  ID3D11Texture2D* texture() { return tex_.Get(); }

private:
  ComPtr<ID3D11Texture2D> tex_;
  ComPtr<ID3D11ShaderResourceView> srv_;

  std::weak_ptr<context> ctx_;
};

class geometry {
public:
  geometry(D3D_PRIMITIVE_TOPOLOGY primitive, 
      uint32_t vertices, uint32_t stride, ID3D11Buffer* buffer)
    : primitive_(primitive), vertices_(vertices), stride_(stride), 
      buffer_(buffer) {}


  void bind(const std::shared_ptr<context>& ctx) {
    ctx_ = ctx;
    
    uint32_t offset = 0;
    (*ctx)->IASetVertexBuffers(0, 1, buffer_.GetAddressOf(), &stride_, &offset);
    (*ctx)->IASetPrimitiveTopology(primitive_);
  }

  void unbind() {}

  void draw() {
    auto ctx = ctx_.lock();
    (*ctx)->Draw(vertices_, 0);
  }

private:
  D3D_PRIMITIVE_TOPOLOGY primitive_;
  uint32_t vertices_;
  uint32_t stride_;
  ComPtr<ID3D11Buffer> buffer_;

  std::weak_ptr<context> ctx_;
};

class default_vsh_traits {
public:
  static const char* code() {
    return R"--(struct VS_INPUT
{
	float4 pos : POSITION;
	float2 tex : TEXCOORD0;
};

struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT output;
	output.pos = input.pos;
	output.tex = input.tex;
	return output;
})--";
  }

  static const char* entry() {
    return "main";
  }

  static const char* model() {
    return "vs_4_0";
  }
};

class default_psh_traits {
public:
  static const char* code() {
    return R"--(Texture2D tex0 : register(t0);
SamplerState samp0 : register(s0);

struct VS_OUTPUT
{
	float4 pos : SV_POSITION;
	float2 tex : TEXCOORD0;
};

float4 main(VS_OUTPUT input) : SV_Target
{
	return tex0.Sample(samp0, input.tex);
})--";

  }

  static const char* entry() {
    return "main";
  }

  static const char* model() {
    return "ps_4_0";
  }
};

class effect {
public:
  effect(ID3D11VertexShader* vsh, 
         ID3D11PixelShader* psh, 
         ID3D11InputLayout* layout) 
    : vsh_(vsh), psh_(psh), layout_(layout) {}

  void bind(const std::shared_ptr<context>& ctx) {
    ctx_ = ctx;
    (*ctx)->IASetInputLayout(layout_.Get());
    (*ctx)->VSSetShader(vsh_.Get(), nullptr, 0);
    (*ctx)->PSSetShader(psh_.Get(), nullptr, 0);
  }

  void unbind() {}

private:
  ComPtr<ID3D11VertexShader> vsh_;
  ComPtr<ID3D11PixelShader> psh_;
  ComPtr<ID3D11InputLayout> layout_;
  std::weak_ptr<context> ctx_;
};

class framebuffer {
public:
  framebuffer(const std::shared_ptr<texture2d>& tex) : buffer_(tex) {}

  void resize(const std::shared_ptr<context>& ctx, int width, int height) {
    buffer_->resize(ctx, width, height);
  }

  std::shared_ptr<texture2d>& texture() { return buffer_; }

private:
  std::shared_ptr<texture2d> buffer_;
};

class layer {
public:
  layer(const std::shared_ptr<geometry>& quad, 
      const std::shared_ptr<effect>& fx, 
      const std::shared_ptr<texture2d>& tex) 
    : geometry_(quad), effect_(fx) {
    framebuffer_ = std::make_shared<framebuffer>(tex);
  }
  
  void render(const std::shared_ptr<context>& ctx) {
    auto tex = framebuffer_->texture();
    if (geometry_ && effect_ && tex) {
      scoped_binder<geometry> quad_binder(ctx, geometry_);
      scoped_binder<effect> fx_binder(ctx, effect_);
      scoped_binder<texture2d> tex_binder(ctx, tex);

      geometry_->draw();
    }
  }

  void resize(const std::shared_ptr<context>& ctx, int width, int height) {
    framebuffer_->resize(ctx, width, height);
  }

private:
  bool flip_;

  std::shared_ptr<effect> effect_;
  std::shared_ptr<geometry> geometry_;
  std::shared_ptr<framebuffer> framebuffer_;
};

class composition {
 public:
  composition() {}

  void render(const std::shared_ptr<context>& ctx) {
    for (const auto& l : layers_) {
      l->render(ctx);
    }
  }

  void resize(const std::shared_ptr<context>& ctx, int width, int height) {
    for (const auto &l : layers_) {
      l->resize(ctx, width, height);
    }
  }

  void add_layer(const std::shared_ptr<layer>& layer) {
    layers_.push_back(layer);
  }

  void remove_layer(const std::shared_ptr<layer>& layer) {
    for (auto it = layers_.begin(); it != layers_.end(); ++it) {
      if (*it == layer) {
        layers_.erase(it++);
        break;
      }
    }
  }

 private:
  std::vector< std::shared_ptr<layer> > layers_;
};

class device {
  struct SimpleVertex {
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT2 tex;
  };

public:
  device(const std::shared_ptr<context>& ctx) {
    (*ctx)->GetDevice(dev_.GetAddressOf());
    ctx_ = ctx;
  }

  device(ID3D11Device* dev)
    : dev_(dev) {
    ComPtr<ID3D11DeviceContext> ctx;
    dev_->GetImmediateContext(ctx.GetAddressOf());
    ctx_.reset(new context(ctx.Detach()));
  }

  device(ID3D11Device* dev, ID3D11DeviceContext* ctx)
    : dev_(dev), 
      ctx_(std::make_shared<context>(ctx)) {}

  ~device() = default;
  
public:
  std::shared_ptr<swapchain> make_swapchain(
      HWND window_handle, int width, int height) {
    ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hr = dev_.As(&dxgi_device);
    if (FAILED(hr)) {
      return nullptr;
    }

    ComPtr<IDXGIAdapter> dxgi_adapter;
    hr = dxgi_device->GetAdapter(dxgi_adapter.GetAddressOf());
    if (FAILED(hr)) {
      return nullptr;
    }

    ComPtr<IDXGIFactory1> dxgi_factory1;
    hr = dxgi_adapter->GetParent(__uuidof(IDXGIFactory1), 
        reinterpret_cast<void**>(dxgi_factory1.GetAddressOf()));
    if (FAILED(hr)) {
      return nullptr;
    }

    ComPtr<IDXGIFactory2> dxgi_factory2;
    hr = dxgi_factory1.As(&dxgi_factory2);
    if (FAILED(hr)) {
      return nullptr;
    }

    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {0};
    swapchain_desc.Width = width;
    swapchain_desc.Height = height;
    swapchain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapchain_desc.Stereo = FALSE;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.SampleDesc.Quality = 0;
    swapchain_desc.BufferUsage = 
        DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_BACK_BUFFER;
    swapchain_desc.BufferCount = 2;
    swapchain_desc.Scaling = DXGI_SCALING_STRETCH;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    swapchain_desc.Flags = 0;
    
    
    ComPtr<IDXGISwapChain1> dxgi_swapchain1;

    //swapchain_desc.BufferCount = 1;
    //swapchain_desc.Scaling = DXGI_SCALING_STRETCH;
    //swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
    swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    hr = dxgi_factory2->CreateSwapChainForHwnd(dev_.Get(), window_handle, 
        &swapchain_desc, nullptr, nullptr, dxgi_swapchain1.GetAddressOf());
    if (!com_check(hr)) {
      return nullptr;
    }

    ComPtr<IDXGISwapChain> dxgi_swapchain;
    hr = dxgi_swapchain1.As(&dxgi_swapchain);
    if (FAILED(hr)) {
      return nullptr;
    }

    // 阻止 alt+enter 快捷键
    dxgi_factory1->MakeWindowAssociation(window_handle, DXGI_MWA_NO_ALT_ENTER);

    // 创建默认渲染目标
    ComPtr<ID3D11Texture2D> back_buffer;
    hr = dxgi_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), 
        reinterpret_cast<void**>(back_buffer.GetAddressOf()));
    if (FAILED(hr)) {
      return nullptr;
    }

    ComPtr<ID3D11RenderTargetView> rtv;
    hr = dev_->CreateRenderTargetView(back_buffer.Get(), nullptr, 
        rtv.GetAddressOf());
    if (FAILED(hr)) {
      return nullptr;
    }

    (*ctx_)->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);

    // 设置视口
    D3D11_VIEWPORT viewport = {0};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = D3D11_MIN_DEPTH;
    viewport.MaxDepth = D3D11_MAX_DEPTH;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    (*ctx_)->RSSetViewports(1, &viewport);

    // 创建默认采样器
    ComPtr<ID3D11SamplerState> sampler;
    {
      D3D11_SAMPLER_DESC desc = {};
      desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
      desc.MinLOD = 0.0f;
      desc.MaxLOD = D3D11_FLOAT32_MAX;
      desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      dev_->CreateSamplerState(&desc, sampler.GetAddressOf());
    }

    // 创建默认混合器
    ComPtr<ID3D11BlendState> blender;
    {
      D3D11_BLEND_DESC desc;
      desc.AlphaToCoverageEnable = FALSE;
      desc.IndependentBlendEnable = FALSE;
      const auto count = sizeof(desc.RenderTarget) / sizeof(desc.RenderTarget[0]);
      for (size_t n = 0; n < count; ++n) {
        desc.RenderTarget[n].BlendEnable = TRUE;
        desc.RenderTarget[n].SrcBlend = D3D11_BLEND_ONE;
        desc.RenderTarget[n].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[n].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[n].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[n].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[n].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[n].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
      }
      dev_->CreateBlendState(&desc, blender.GetAddressOf());
    }

    return std::make_shared<swapchain>(width, height, dxgi_device.Detach(), 
        dxgi_swapchain1.Detach(), rtv.Detach(), 
        sampler.Detach(), blender.Detach());
  }

  std::shared_ptr<swapchain> make_swapchain_for_composition(
      HWND window_handle, int width, int height) {
    ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hr = dev_.As(&dxgi_device);
    if (FAILED(hr)) {
      return nullptr;
    }

    ComPtr<IDXGIAdapter> dxgi_adapter;
    hr = dxgi_device->GetAdapter(dxgi_adapter.GetAddressOf());
    if (FAILED(hr)) {
      return nullptr;
    }

    ComPtr<IDXGIFactory1> dxgi_factory1;
    hr = dxgi_adapter->GetParent(__uuidof(IDXGIFactory1), 
        reinterpret_cast<void**>(dxgi_factory1.GetAddressOf()));
    if (FAILED(hr)) {
      return nullptr;
    }

    ComPtr<IDXGIFactory2> dxgi_factory2;
    hr = dxgi_factory1.As(&dxgi_factory2);
    if (FAILED(hr)) {
      return nullptr;
    }

    DXGI_SWAP_CHAIN_DESC1 swapchain_desc = {0};
    swapchain_desc.Width = width;
    swapchain_desc.Height = height;
    swapchain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapchain_desc.Stereo = FALSE;
    swapchain_desc.SampleDesc.Count = 1;
    swapchain_desc.SampleDesc.Quality = 0;
    swapchain_desc.BufferUsage = 
        DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_BACK_BUFFER;
    swapchain_desc.BufferCount = 2;
    swapchain_desc.Scaling = DXGI_SCALING_STRETCH;
    swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    swapchain_desc.Flags = 0;
    
    
    ComPtr<IDXGISwapChain1> dxgi_swapchain1;
    hr = E_FAIL;
    hr = dxgi_factory2->CreateSwapChainForComposition(dev_.Get(), &swapchain_desc,
        nullptr, dxgi_swapchain1.GetAddressOf());
    if (FAILED(hr)) {
      swapchain_desc.BufferCount = 1;
      swapchain_desc.Scaling = DXGI_SCALING_STRETCH;
      swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
      swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
      hr = dxgi_factory2->CreateSwapChainForHwnd(dev_.Get(), window_handle, 
          &swapchain_desc, nullptr, nullptr, dxgi_swapchain1.GetAddressOf());
    }
    if (!com_check(hr)) {
      return nullptr;
    }

    ComPtr<IDXGISwapChain> dxgi_swapchain;
    hr = dxgi_swapchain1.As(&dxgi_swapchain);
    if (FAILED(hr)) {
      return nullptr;
    }

    // 阻止 alt+enter 快捷键
    dxgi_factory1->MakeWindowAssociation(window_handle, DXGI_MWA_NO_ALT_ENTER);

    // 创建默认渲染目标
    ComPtr<ID3D11Texture2D> back_buffer;
    hr = dxgi_swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), 
        reinterpret_cast<void**>(back_buffer.GetAddressOf()));
    if (FAILED(hr)) {
      return nullptr;
    }

    ComPtr<ID3D11RenderTargetView> rtv;
    hr = dev_->CreateRenderTargetView(back_buffer.Get(), nullptr, 
        rtv.GetAddressOf());
    if (FAILED(hr)) {
      return nullptr;
    }

    (*ctx_)->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);

    // 设置视口
    D3D11_VIEWPORT viewport = {0};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MinDepth = D3D11_MIN_DEPTH;
    viewport.MaxDepth = D3D11_MAX_DEPTH;
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    (*ctx_)->RSSetViewports(1, &viewport);

    // 创建默认采样器
    ComPtr<ID3D11SamplerState> sampler;
    {
      D3D11_SAMPLER_DESC desc = {};
      desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
      desc.MinLOD = 0.0f;
      desc.MaxLOD = D3D11_FLOAT32_MAX;
      desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      dev_->CreateSamplerState(&desc, sampler.GetAddressOf());
    }

    // 创建默认混合器
    ComPtr<ID3D11BlendState> blender;
    {
      D3D11_BLEND_DESC desc;
      desc.AlphaToCoverageEnable = FALSE;
      desc.IndependentBlendEnable = FALSE;
      const auto count = sizeof(desc.RenderTarget) / sizeof(desc.RenderTarget[0]);
      for (size_t n = 0; n < count; ++n) {
        desc.RenderTarget[n].BlendEnable = TRUE;
        desc.RenderTarget[n].SrcBlend = D3D11_BLEND_ONE;
        desc.RenderTarget[n].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[n].SrcBlendAlpha = D3D11_BLEND_ONE;
        desc.RenderTarget[n].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        desc.RenderTarget[n].BlendOp = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[n].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[n].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
      }
      dev_->CreateBlendState(&desc, blender.GetAddressOf());
    }

    return std::make_shared<swapchain>(width, height, dxgi_device.Detach(), 
        dxgi_swapchain1.Detach(), rtv.Detach(), 
        sampler.Detach(), blender.Detach());
  }

  template< typename Texture2DTraits >
  std::shared_ptr<texture2d> make_texture2d(int width, int height) {
    D3D11_TEXTURE2D_DESC td;
    td.ArraySize = 1;
    td.BindFlags = Texture2DTraits::bind_flags();
    td.CPUAccessFlags = 0;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.MiscFlags = Texture2DTraits::misc_flags();
    td.SampleDesc.Count = 1;
    td.SampleDesc.Quality = 0;
    td.Usage = D3D11_USAGE_DEFAULT;

    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = dev_->CreateTexture2D(&td, nullptr, tex.GetAddressOf());
    if (FAILED(hr)) {
      return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    srv_desc.Format = td.Format;
    srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Texture2D.MostDetailedMip = 0;
    srv_desc.Texture2D.MipLevels = 1;

    ComPtr<ID3D11ShaderResourceView> srv;
    hr = dev_->CreateShaderResourceView(tex.Get(), &srv_desc, 
        srv.GetAddressOf());
    if (FAILED(hr)) {
      return nullptr;
    }

    return std::make_shared<texture2d>(tex.Detach(), srv.Detach());
  }

  std::shared_ptr<texture2d> open_shared_texture2d(void* handle) {
    ID3D11Texture2D* tex = nullptr;
    auto hr = dev_->OpenSharedResource(handle, __uuidof(ID3D11Texture2D),
                                          (void**)(&tex));
    if (FAILED(hr)) {
      return nullptr;
    }
    
    D3D11_TEXTURE2D_DESC td;
    tex->GetDesc(&td);
    
    ID3D11ShaderResourceView* srv = nullptr;
    
    if (td.BindFlags & D3D11_BIND_SHADER_RESOURCE) {
      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
      srv_desc.Format = td.Format;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MostDetailedMip = 0;
      srv_desc.Texture2D.MipLevels = 1;
    
      hr = dev_->CreateShaderResourceView(tex, &srv_desc, &srv);
      if (FAILED(hr)) {
        tex->Release();
        return nullptr;
      }
    }
    
    return std::make_shared<texture2d>(tex, srv);
  }

  std::shared_ptr<geometry> create_quad(
      float x, float y, float width, float height, bool flip) {
    x = (x * 2.0f) - 1.0f;
    y = 1.0f - (y * 2.0f);
    width = width * 2.0f;
    height = height * 2.0f;
    float z = 1.0f;

    SimpleVertex vertices[] = {

        {DirectX::XMFLOAT3(x, y, z), DirectX::XMFLOAT2(0.0f, 0.0f)},
        {DirectX::XMFLOAT3(x + width, y, z), DirectX::XMFLOAT2(1.0f, 0.0f)},
        {DirectX::XMFLOAT3(x, y - height, z), DirectX::XMFLOAT2(0.0f, 1.0f)},
        {DirectX::XMFLOAT3(x + width, y - height, z),
         DirectX::XMFLOAT2(1.0f, 1.0f)}};

    if (flip) {
      DirectX::XMFLOAT2 tmp(vertices[2].tex);
      vertices[2].tex = vertices[0].tex;
      vertices[0].tex = tmp;

      tmp = vertices[3].tex;
      vertices[3].tex = vertices[1].tex;
      vertices[1].tex = tmp;
    }

    D3D11_BUFFER_DESC desc = {};
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.ByteWidth = sizeof(SimpleVertex) * 4;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA srd = {};
    srd.pSysMem = vertices;

    ID3D11Buffer* buffer = nullptr;
    const auto hr = dev_->CreateBuffer(&desc, &srd, &buffer);
    if (SUCCEEDED(hr)) {
      return std::make_shared<geometry>(
          D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, 4,
          static_cast<uint32_t>(sizeof(SimpleVertex)), buffer);
    }

    return nullptr;
  }
  
  template< typename VertexShaderTraits, typename PixelShaderTraits >
  std::shared_ptr<effect>create_effect() {
    ComPtr<ID3DBlob> vs_blob = compile_shader(VertexShaderTraits::code(),
        VertexShaderTraits::entry(), VertexShaderTraits::model());

    ID3D11VertexShader* vshdr = nullptr;
    ID3D11InputLayout* layout = nullptr;

    if (vs_blob) {
      dev_->CreateVertexShader(vs_blob->GetBufferPointer(),
                                  vs_blob->GetBufferSize(), nullptr, &vshdr);

      D3D11_INPUT_ELEMENT_DESC layout_desc[] = {
          {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
           D3D11_INPUT_PER_VERTEX_DATA, 0},
          {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12,
           D3D11_INPUT_PER_VERTEX_DATA, 0},
      };

      UINT elements = ARRAYSIZE(layout_desc);

      dev_->CreateInputLayout(layout_desc, elements,
                                 vs_blob->GetBufferPointer(),
                                 vs_blob->GetBufferSize(), &layout);
    }

    const auto ps_blob = compile_shader(PixelShaderTraits::code(), 
        PixelShaderTraits::entry(), PixelShaderTraits::model());
    ID3D11PixelShader* pshdr = nullptr;
    if (ps_blob) {
      dev_->CreatePixelShader(ps_blob->GetBufferPointer(),
                                 ps_blob->GetBufferSize(), nullptr, &pshdr);
    }

    return std::make_shared<effect>(vshdr, pshdr, layout);
  }


  std::shared_ptr<context>& immedidate_context() { return ctx_; }

  operator ID3D11Device*() { return dev_.Get(); }
  ID3D11Device* operator->() { return dev_.Get(); }

public:
  static std::shared_ptr<device> make() {
    D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1, 
      D3D_FEATURE_LEVEL_10_0
    };
    UINT num_feature_levels = 
        sizeof(feature_levels) / sizeof(feature_levels[0]);

    D3D_FEATURE_LEVEL selected_level;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;

    using D3D11CreateDeviceProc = HRESULT (WINAPI*) (
        IDXGIAdapter* pAdapter,
        D3D_DRIVER_TYPE DriverType,
        HMODULE Software,
        UINT Flags,
        CONST D3D_FEATURE_LEVEL* pFeatureLevels,
        UINT FeatureLevels,
        UINT SDKVersion,
        ID3D11Device** ppDevice,
        D3D_FEATURE_LEVEL* pFeatureLevel,
        ID3D11DeviceContext** ppImmediateContext);

    HMODULE lib_d3d11 = LoadLibraryW(L"d3d11.dll");
    if (lib_d3d11 == NULL) {
      return nullptr;
    }

    D3D11CreateDeviceProc d3d11_create_device = 
        reinterpret_cast<D3D11CreateDeviceProc>(
            GetProcAddress(lib_d3d11, "D3D11CreateDevice"));
    if (!d3d11_create_device) {
      return nullptr;
    }

    HRESULT hr = d3d11_create_device(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, 
        feature_levels, num_feature_levels, D3D11_SDK_VERSION, 
        dev.GetAddressOf(), &selected_level, ctx.GetAddressOf());
  
    if (SUCCEEDED(hr)) {
      assert(selected_level >= D3D_FEATURE_LEVEL_10_0);
      return std::make_shared<device>(dev.Detach(), ctx.Detach());
    }

    return nullptr;
  }

private:
  static ID3DBlob* compile_shader(const char* code, const char* entry_point,
      const char* model) {
    HMODULE lib_compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    if (!lib_compiler) {
      return nullptr;
    }

    using D3DCompileProc = HRESULT(WINAPI*)(
        LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR,
        LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

    D3DCompileProc fnc_compile = reinterpret_cast<D3DCompileProc>(
        GetProcAddress(lib_compiler, "D3DCompile"));
    if (!fnc_compile) {
      return nullptr;
    }

    DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS;

  #if defined(NDEBUG)
  // flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
  // flags |= D3DCOMPILE_AVOID_FLOW_CONTROL;
  #else
    flags |= D3DCOMPILE_DEBUG;
    flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
  #endif

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> blob_err;

    size_t len = strlen(code) + 1;

    HRESULT hr =
        fnc_compile(code, len, nullptr, nullptr, nullptr, entry_point,
                    model, flags, 0, &blob, &blob_err);

    if (FAILED(hr)) {
      return nullptr;
    }

    return blob.Detach();
  }

private:
  ComPtr<ID3D11Device> dev_;
  std::shared_ptr<context> ctx_;
};

} // namespace d3d11
} // namespace windows
} // namespace platform
} // namespace calf

#endif // CALF_PLATFORM_WINDOWS_D3D11_HPP_
