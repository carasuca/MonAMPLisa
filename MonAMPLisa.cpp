#include "MonAMPLisa.h"

#define GOT_NET // no net? no worries!

struct Image : std::vector<BYTE>
{ // assume 8-bit mono
	Image() = default;
#ifdef GOT_NET
	bool FromNet(const wchar_t url[]);
	bool FromMemory(const void*, unsigned);
#else
	bool FromNet(const wchar_t[]) {return false;}
#endif
	bool FromFile(const wchar_t path[]);

	bool SavePNG(const wchar_t path[]);

	unsigned height() const { return width  ? (unsigned)size() / width : 0; }

	unsigned width=0;
};



int wmain(int c, wchar_t *v[])
{
	CoInitialize(0);

	Image im;

	wchar_t path[MAX_PATH]
		= L"https://raw.githubusercontent.com/gynvael/stream/master/028-monaliza-genetycznie/mona_small_gray.png";
		//= L"http://lorempixel.com/256/256/";

	if (c > 1)
	{
		if (im.FromFile(v[1])) 
			wcscpy_s(path, v[1]);
		else
		{
			wprintf(L"Can't load: %s\n", v[1]);
			c = 1;
		}
	}

	if (c < 2 && !im.FromNet(path))
		return wprintf(L"Can't get: %s\nBYE!\n", path);


	static const int sel = 2, pop = 256;
	const unsigned leap = 1000, limit = -1;

	const unsigned height = im.height(), width = im.width;
	wprintf(L"source: %ix%i %s\n", width, height, path);
	wprintf(L"select %i from %i\noutput: every %i generation up to %i\n", sel, pop, leap, limit);

	MonAMPLisa<sel, pop> impl(im.data(), width, height);

#	define OUT_DIR L"out"
	::CreateDirectory(OUT_DIR, 0);
	::ShellExecute(::GetConsoleWindow(), 0, OUT_DIR, 0, 0, SW_SHOWNOACTIVATE);

	impl.GetSource(im.data(), im.size());
	im.SavePNG(OUT_DIR L"\\source.png");

	LARGE_INTEGER now, last = {};
	::QueryPerformanceFrequency(&now);
	::QueryPerformanceCounter(&last);

	const double scale = 1000.0 / now.QuadPart / leap;

	for (unsigned i = 0; i < limit; i++)
	{
		auto avg = impl.Step(i);
		printf("\rG:%i avg:%i ", i, avg);
		if (!(i % leap))
		{
			::QueryPerformanceCounter(&now);

			double ms = (now.QuadPart - last.QuadPart) * scale;
			swprintf_s(path, OUT_DIR L"\\%.4i.png", i);
			wprintf(L" %.4f ms/g %s\n", ms, path);
			
			impl.GetPreview(im.data(), im.size());
			im.SavePNG(path);
			
			last = now; // image save included
		}
	}

	return 0;
}






// IMAGE DATA TRANSFER APPENDED HERE

#include <wincodec.h>  // WIC
#include <atlcomcli.h> // CComPtr

bool Image::SavePNG(const wchar_t path[])
{
	// https://github.com/carasuca/MinimalOffscreenD3D/blob/master/MinimalOffscreenD3D.cpp
	static CComPtr<IWICImagingFactory> factory;
	static HRESULT hr = factory.CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER);
	if (FAILED(hr)) return false;

	CComPtr<IWICBitmap> bitmap;
	hr = factory->CreateBitmapFromMemory(
		width, height(),
		GUID_WICPixelFormat8bppGray, width, (UINT)size(),
		data(), &bitmap);
	if (FAILED(hr)) return false;

	CComPtr<IWICStream> stream;
	CComPtr<IWICBitmapEncoder> encoder;
	CComPtr<IWICBitmapFrameEncode> frame;

	hr = factory->CreateStream(&stream);
	if (FAILED(hr)) return false;
	hr = stream->InitializeFromFilename(path, GENERIC_WRITE);
	if (FAILED(hr)) return false;
	hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
	if (FAILED(hr)) return false;
	hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
	if (FAILED(hr)) return false;

	hr = encoder->CreateNewFrame(&frame, 0);
	if (FAILED(hr)) return false;
	hr = frame->Initialize(0);
	if (FAILED(hr)) return false;

	hr = frame->WriteSource(bitmap, nullptr);
	if (FAILED(hr)) return false;

	hr = frame->Commit();
	hr = encoder->Commit();
	if (FAILED(hr)) return false;

	return true;

}

bool FromDecoder(IWICBitmapDecoder* decoder, IWICImagingFactory* wic, Image& im)
{
	CComPtr<IWICBitmapFrameDecode> frame;
	HRESULT hr = decoder->GetFrame(0, &frame);

	if (FAILED(hr)) return false;

	UINT W, H;
	hr = frame->GetSize(&W, &H);
	if (FAILED(hr)) return false;

	CComPtr<IWICFormatConverter> conv;
	hr = wic->CreateFormatConverter(&conv);
	if (FAILED(hr)) return false;

	hr = conv->Initialize(
		frame,                           // Source frame to convert
		GUID_WICPixelFormat8bppGray,     // The desired pixel format
		WICBitmapDitherTypeNone,         // The desired dither pattern
		NULL,                            // The desired palette 
		0.f,                             // The desired alpha threshold
		WICBitmapPaletteTypeCustom       // Palette translation type
		);

	if (FAILED(hr)) return false;

	im.width = W;
	im.resize(W*H);
	hr = conv->CopyPixels(0, W, (UINT)im.size(), im.data());
	if (FAILED(hr)) return false;

	return true;
}

bool Image::FromFile(const wchar_t path[])
{
	// http://www.nuonsoft.com/blog/2011/10/17/introduction-to-wic-how-to-use-wic-to-load-an-image-and-draw-it-with-gdi/comment-page-1/
	CComPtr<IWICImagingFactory> wic;
	HRESULT hr = wic.CoCreateInstance(CLSID_WICImagingFactory);
	if (FAILED(hr)) return false;

	CComPtr<IWICBitmapDecoder> decoder;
	hr = wic->CreateDecoderFromFilename(
		path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
	if (FAILED(hr)) return false;

	return FromDecoder(decoder, wic, *this);
}

#ifdef GOT_NET

bool Image::FromMemory(const void* ptr, unsigned bytes)
{
	CComPtr<IWICImagingFactory> wic;
	HRESULT hr = wic.CoCreateInstance(CLSID_WICImagingFactory);
	if (FAILED(hr)) return false;

	CComPtr<IWICStream> stream;
	hr = wic->CreateStream(&stream);
	if (FAILED(hr)) return false;

	hr = stream->InitializeFromMemory((WICInProcPointer)ptr, bytes);
	if (FAILED(hr)) return false;

	CComPtr<IWICBitmapDecoder> decoder;
	hr = wic->CreateDecoderFromStream(stream, 0, WICDecodeMetadataCacheOnDemand, &decoder);
	if (FAILED(hr)) return false;

	return FromDecoder(decoder, wic, *this);
}

#include <MsXml6.h>
#include <comutil.h>

// https://www.ohadpr.com/2006/03/solution-to-unresolved-external-symbol-_com_issue_error-_bstr_t_bstr_t/
#pragma comment(lib, "comsupp") 
#pragma comment(lib, "msxml6")


typedef std::vector<BYTE> Buffer;

bool GetFileHTTP(const wchar_t url[], Buffer& buffer)
{
	// http://stackoverflow.com/questions/822714/how-to-download-a-file-with-winhttp-in-c-c
	// -> https://msdn.microsoft.com/en-us/library/ms759148(v=vs.85).aspx
	CComPtr<IXMLHTTPRequest> req;
	HRESULT hr = req.CoCreateInstance(CLSID_XMLHTTPRequest);
	if (FAILED(hr)) return false;

	// http://www.roblocher.com/whitepapers/oletypes.html
	hr = req->open(L"GET", _bstr_t(url), _variant_t(VARIANT_FALSE), _variant_t(L""), _variant_t(L""));
	if (FAILED(hr)) return false;

	hr = req->send(_variant_t());
	if (FAILED(hr)) return false;

	_variant_t resp;
	hr = req->get_responseBody(&resp);
	if (FAILED(hr)) return false;

	// no idea what's happening
	if (resp.vt & VT_ARRAY)
	{
		// http://stackoverflow.com/questions/3730840/how-to-create-and-initialize-safearray-of-doubles-in-c-to-pass-to-c-sharp
		auto ptr = (BYTE*)resp.parray->pvData;
		buffer.assign(ptr, ptr + resp.parray->rgsabound->cElements);
		SafeArrayUnaccessData(resp.parray);
	}

	return true;
}

bool Image::FromNet(const wchar_t url[])
{
	Buffer buffer;
	if (!GetFileHTTP(url, buffer)) return false;

	return FromMemory(buffer.data(), (unsigned)buffer.size());
}
#endif