#include "DawnInternal.h"

#if LUTE_UI_USE_DAWN && LUTE_UI_USE_HARFBUZZ_GPU

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

#include <limits>

namespace lute::ui::dawn
{

#if defined(__APPLE__)
class ImageIoImageGlyphDecoder final : public ImageGlyphDecoder
{
public:
    std::optional<DecodedImage> decode(const ImageGlyphDecodeRequest& request) override
    {
        if (request.bytes.empty())
            return std::nullopt;

        CFDataRef cfData = CFDataCreate(
            kCFAllocatorDefault,
            reinterpret_cast<const UInt8*>(request.bytes.data()),
            static_cast<CFIndex>(request.bytes.size())
        );
        if (!cfData)
            return std::nullopt;

        CGImageSourceRef source = CGImageSourceCreateWithData(cfData, nullptr);
        CFRelease(cfData);
        if (!source)
            return std::nullopt;

        CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
        CFRelease(source);
        if (!image)
            return std::nullopt;

        size_t width = CGImageGetWidth(image);
        size_t height = CGImageGetHeight(image);
        if (width == 0 || height == 0 || width > request.maxWidth || height > request.maxHeight ||
            width > std::numeric_limits<size_t>::max() / height / 4)
        {
            CGImageRelease(image);
            return std::nullopt;
        }

        DecodedImage decoded;
        decoded.width = static_cast<uint32_t>(width);
        decoded.height = static_cast<uint32_t>(height);
        decoded.pixels.assign(width * height * 4, 0);

        CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context = CGBitmapContextCreate(
            decoded.pixels.data(),
            width,
            height,
            8,
            width * 4,
            colorSpace,
            static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Big) | static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast)
        );

        if (colorSpace)
            CGColorSpaceRelease(colorSpace);

        if (!context)
        {
            CGImageRelease(image);
            return std::nullopt;
        }

        CGContextDrawImage(context, CGRectMake(0.0, 0.0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), image);
        CGContextRelease(context);
        CGImageRelease(image);
        return decoded;
    }
};
#else
class NullImageGlyphDecoder final : public ImageGlyphDecoder
{
public:
    std::optional<DecodedImage> decode(const ImageGlyphDecodeRequest&) override
    {
        return std::nullopt;
    }
};
#endif

ImageGlyphDecoder& imageGlyphDecoder()
{
#if defined(__APPLE__)
    static ImageIoImageGlyphDecoder decoder;
#else
    static NullImageGlyphDecoder decoder;
#endif
    return decoder;
}

std::optional<DecodedImage> decodeImageGlyphBlob(hb_blob_t* blob, ImageGlyphBlobFormat format)
{
    if (!blob)
        return std::nullopt;

    unsigned int length = 0;
    const char* data = hb_blob_get_data(blob, &length);
    if (!data || length == 0)
        return std::nullopt;

    ImageGlyphDecodeRequest request;
    request.bytes = std::string_view(data, length);
    request.format = format;
    return imageGlyphDecoder().decode(request);
}

} // namespace lute::ui::dawn
#endif
