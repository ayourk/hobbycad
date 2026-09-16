// =====================================================================
//  tools/render-icons.cpp — render the application icon from its SVG
//  SPDX-License-Identifier: GPL-3.0-only
//  Part of HobbyCAD (ayourk/hobbycad)
// =====================================================================
//
//    render-icons <in.svg> <outdir> <name> <size>... [ico=<size>,<size>,...]
//                 [xpm=<size>]
//
//  writes  <outdir>/<name>-<size>.png   for every size given
//          <outdir>/<name>.ico          when "ico=" lists the sizes to pack
//          <outdir>/<name>.xpm          when "xpm=" gives the menu icon size
//
//  Why a tool of our own rather than rsvg-convert and icotool in CMake:
//  those are two more build dependencies on every packaging target, and
//  a machine without a working rsvg-convert silently shipped a package
//  with no icons at all (2026-09-12). Qt renders SVG and writes PNG, and
//  Qt Gui and Qt Svg are already required for the GUI, so this adds nothing
//  to any build's dependencies. The ICO container is simple enough to write
//  here: a directory of entries followed by PNG payloads, the form Windows
//  Vista and later read for every size.
//
//  The Debian menu icon is an XPM of at most 32x32. Menu loaders disagree
//  on transparency masks, so it is rendered onto white, reduced to a
//  palette when the render has more than 256 colors, and written by Qt's
//  own XPM writer. It used to be a committed ImageMagick conversion that
//  drifted from the SVG.
//
//  Runs offscreen: no display is needed, which matters in a build container.
// =====================================================================

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>
#include <QSet>

#include <cstdint>
#include <vector>

namespace {

QImage render(QSvgRenderer& svg, int size)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    svg.render(&painter);
    painter.end();
    return image;
}

QByteArray pngBytes(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

void putU16(QByteArray& out, uint16_t v)
{
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
}

void putU32(QByteArray& out, uint32_t v)
{
    for (int i = 0; i < 4; ++i) out.append(static_cast<char>((v >> (8 * i)) & 0xFF));
}

/// A multi-resolution ICO with PNG-compressed entries: ICONDIR (6 bytes),
/// one 16-byte ICONDIRENTRY per image, then the images.
bool writeIco(QSvgRenderer& svg, const std::vector<int>& sizes, const QString& path, QTextStream& err)
{
    std::vector<QByteArray> payloads;
    payloads.reserve(sizes.size());
    for (int size : sizes) payloads.push_back(pngBytes(render(svg, size)));

    QByteArray out;
    putU16(out, 0);                                       // reserved
    putU16(out, 1);                                       // type: icon
    putU16(out, static_cast<uint16_t>(sizes.size()));
    uint32_t offset = 6 + 16 * static_cast<uint32_t>(sizes.size());
    for (size_t i = 0; i < sizes.size(); ++i) {
        const int size = sizes[i];
        out.append(static_cast<char>(size >= 256 ? 0 : size));   // width, 0 means 256
        out.append(static_cast<char>(size >= 256 ? 0 : size));   // height
        out.append(static_cast<char>(0));                         // palette colors: none
        out.append(static_cast<char>(0));                         // reserved
        putU16(out, 1);                                           // color planes
        putU16(out, 32);                                          // bits per pixel
        putU32(out, static_cast<uint32_t>(payloads[i].size()));
        putU32(out, offset);
        offset += static_cast<uint32_t>(payloads[i].size());
    }
    for (const QByteArray& p : payloads) out.append(p);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(out) != out.size()) {
        err << "render-icons: cannot write " << path << "\n";
        return false;
    }
    return true;
}

/// The Debian menu icon: the SVG rendered onto white (no mask), reduced to
/// an indexed palette only when it needs one, written by Qt's XPM writer.
bool writeXpm(QSvgRenderer& svg, int size, const QString& path, QTextStream& err)
{
    QImage flat(size, size, QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter painter(&flat);
    painter.drawImage(0, 0, render(svg, size));
    painter.end();

    QSet<QRgb> colors;
    for (int y = 0; y < flat.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(flat.constScanLine(y));
        for (int x = 0; x < flat.width(); ++x) colors.insert(row[x]);
    }
    // Qt keeps an exact palette when 256 colors suffice; beyond that it
    // quantizes, and threshold dithering keeps a small icon free of noise.
    if (colors.size() > 256)
        flat = flat.convertToFormat(QImage::Format_Indexed8, Qt::ThresholdDither | Qt::AvoidDither);

    if (!flat.save(path, "XPM")) {
        err << "render-icons: cannot write " << path << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QTextStream err(stderr);

    if (argc < 5) {
        err << "usage: render-icons <in.svg> <outdir> <name> <size>... [ico=<size>,<size>,...] [xpm=<size>]\n";
        return 2;
    }
    const QString source = QString::fromLocal8Bit(argv[1]);
    const QString outDir = QString::fromLocal8Bit(argv[2]);
    const QString name = QString::fromLocal8Bit(argv[3]);

    std::vector<int> pngSizes, icoSizes;
    int xpmSize = 0;
    for (int i = 4; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(QStringLiteral("ico="))) {
            for (const QString& s : arg.mid(4).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                bool ok = false;
                const int v = s.toInt(&ok);
                if (!ok || v <= 0 || v > 256) { err << "render-icons: bad ico size '" << s << "'\n"; return 2; }
                icoSizes.push_back(v);
            }
            continue;
        }
        if (arg.startsWith(QStringLiteral("xpm="))) {
            bool ok = false;
            xpmSize = arg.mid(4).toInt(&ok);
            if (!ok || xpmSize <= 0 || xpmSize > 256) { err << "render-icons: bad xpm size '" << arg.mid(4) << "'\n"; return 2; }
            continue;
        }
        bool ok = false;
        const int v = arg.toInt(&ok);
        if (!ok || v <= 0 || v > 4096) { err << "render-icons: bad size '" << arg << "'\n"; return 2; }
        pngSizes.push_back(v);
    }

    QSvgRenderer svg(source);
    if (!svg.isValid()) {
        err << "render-icons: cannot read " << source << "\n";
        return 1;
    }
    if (!QDir().mkpath(outDir)) {
        err << "render-icons: cannot create " << outDir << "\n";
        return 1;
    }

    for (int size : pngSizes) {
        const QString path = QStringLiteral("%1/%2-%3.png").arg(outDir, name).arg(size);
        if (!render(svg, size).save(path, "PNG")) {
            err << "render-icons: cannot write " << path << "\n";
            return 1;
        }
    }
    if (!icoSizes.empty() && !writeIco(svg, icoSizes, QStringLiteral("%1/%2.ico").arg(outDir, name), err))
        return 1;
    if (xpmSize > 0 && !writeXpm(svg, xpmSize, QStringLiteral("%1/%2.xpm").arg(outDir, name), err))
        return 1;
    return 0;
}
