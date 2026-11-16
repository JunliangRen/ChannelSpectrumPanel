#pragma once
inline void d2d_gradient_tests(IWICImagingFactory* wic,ID2D1Factory* factory){
    int maxDifference=0;
    for(int height:{1,2,6,7,8,19,32,65})for(unsigned rotation=0;rotation<4;++rotation){
        const int width=19,pw=rotation&1?height:width,ph=rotation&1?width:height;
        csp::ComPtr<IWICBitmap> bitmap;
        check(SUCCEEDED(wic->CreateBitmap(pw,ph,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,bitmap.GetAddressOf())),"gradient bitmap creates");
        csp::ComPtr<ID2D1RenderTarget> target;
        check(SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.Get(),D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE),target.GetAddressOf())),"gradient software target creates");
        csp::D2DScene scene;
        csp::Settings s;s.direct2d=true;s.show_peaks=true;s.rotation=rotation;
        s.foreground_colors={0x1030f0,0xf02040,0x40e010,0xf0c020,0x203050,0xe09030,0x40b0e0,0x9060a0};
        s.background_colors={0xabcdef,0x765432,0x102030,0xf0e0d0,0x123456,0x654321,0x345678,0x876543};
        const std::vector<csp::Bar> bars{{1,2,0,0,0,1},{4,7,height/3,height/4,1,.5},{9,10,height-1,height-1,0,1},{12,14,height,height,1,1},{16,17,-2,-1,0,1}};
        // Reuse one scene while editing colors, toggling palettes and switching styles.
        for(int step=0;step<12;++step){
            s.style=step%3==0?csp::Style::Vertical:step%3==1?csp::Style::GradientBars:csp::Style::Simple;
            s.background_palette=step%2==0;s.foreground_palette=step%4!=0;
            s.foreground=0xd04080+step;s.foreground2=0x20c030+step;s.background=0x142536+step;
            s.foreground_colors[3]^=0x123456;s.background_colors[4]^=0x654321;
            const auto saved=csp::encode(s);
            target->SetTransform(csp::d2d_transform(rotation,pw,ph));target->BeginDraw();
            auto hr=scene.draw(target.Get(),width,height,bars,{},s);auto end=target->EndDraw();
            check(SUCCEEDED(hr) && SUCCEEDED(end),"gradient settings switch draws successfully");
            std::vector<uint32_t> actual(width*height),logical(width*height),expected(width*height);
            check(SUCCEEDED(bitmap->CopyPixels(nullptr,pw*4,UINT(actual.size()*4),reinterpret_cast<BYTE*>(actual.data()))),"read gradient pixels");
            csp::render(logical.data(),width,height,bars,s);csp::rotate_pixels(logical.data(),width,height,expected.data(),rotation);
            int difference=0;
            for(size_t i=0;i<actual.size();++i)for(unsigned shift:{0u,8u,16u})difference=std::max(difference,std::abs(int((actual[i]>>shift)&255)-int((expected[i]>>shift)&255)));
            maxDifference=std::max(maxDifference,difference);
            if(difference>2)std::cerr<<"gradient difference="<<difference<<" height="<<height<<" rotation="<<rotation<<" step="<<step<<"\n";
            check(difference<=2,"D2D gradients match GDI within two RGB levels including tiny sizes and rotations");
            check(csp::encode(s)==saved,"gradient drawing does not change serialized settings");
            target->BeginDraw();hr=scene.draw(target.Get(),width,height,bars,{},s);end=target->EndDraw();
            check(SUCCEEDED(hr) && SUCCEEDED(end),"unchanged gradient settings reuse cached resources");
            std::vector<uint32_t> repeated(actual.size());
            check(SUCCEEDED(bitmap->CopyPixels(nullptr,pw*4,UINT(repeated.size()*4),reinterpret_cast<BYTE*>(repeated.data()))) && repeated==actual,"cached gradient frame is identical");
            if(step==5)scene.clear(); // Recreate all target-owned gradients mid-sequence.
        }
        scene.clear();scene.clear();
    }
    using namespace d2d_failure_test;
    Window window;
    for(auto style:{csp::Style::Vertical,csp::Style::GradientBars,csp::Style::Simple}){
        Operations operations;
        {csp::D2DRenderer renderer(operations);csp::Settings s;s.direct2d=true;s.style=style;s.foreground_palette=true;s.background_palette=true;
            const std::vector<csp::Bar> bars{{2,4,3,1,0,1}};
            check(renderer.draw(window.handle,64,32,bars,{},s),"palette target initializes");
            s.foreground_colors[1]^=0xffffff;s.background_colors[6]^=0xffffff;
            check(renderer.draw(window.handle,80,40,bars,{},s),"palette resources update after resize and color edit");
            int fallback=0;s.segmented=true;
            check(!renderer.draw_or_fallback(window.handle,80,40,bars,{},s,[&]{++fallback;}) && fallback==1 && !renderer.failed(),"unsupported effect retains GDI fallback without poisoning target");
            s.segmented=false;operations.end_result=D2DERR_RECREATE_TARGET;
            check(!renderer.draw_or_fallback(window.handle,80,40,bars,{},s,[&]{++fallback;}) && fallback==2,"gradient device loss invokes GDI fallback");
            check(operations.release_probe()==0,"device loss releases palette and per-bar gradient brushes");
            renderer.reset();operations.end_result=S_OK;
            check(renderer.draw(window.handle,64,32,bars,{},s),"gradient target recovers after loss");
        }
        check(operations.release_probe()==0,"destructor releases all new gradient resources");
    }
    std::cout<<"Direct2D gradients: 384 GDI pixel comparisons, setting switches, rotations and resource release passed; max RGB difference "<<maxDifference<<".\n";
}
