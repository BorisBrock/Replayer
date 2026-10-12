// iconrender — Renders the app icon from its SVG sources at build time.
//
// Usage: iconrender <icon.svg> <icon-small.svg> <max small size> <out dir> <size>...
//
// Writes replayer-<size>.png for every size and replayer.ico holding all of
// them. Sizes up to <max small size> come from the simplified SVG, which stays
// legible where the detailed one would turn to mush.

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

QByteArray renderPng(QSvgRenderer& svg, int size) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    svg.render(&painter);
    painter.end();

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

// ICO with PNG-compressed entries, which Windows supports since Vista
bool writeIco(const QString& path, const std::vector<std::pair<int, QByteArray>>& images) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    out << quint16(0) << quint16(1) << quint16(images.size()); // reserved, type icon, count
    quint32 offset = 6 + 16 * quint32(images.size());
    for (const auto& [size, png] : images) {
        const quint8 dim = size >= 256 ? 0 : quint8(size); // 0 means 256
        out << dim << dim << quint8(0) << quint8(0)         // no palette, reserved
            << quint16(1) << quint16(32)                    // planes, bits per pixel
            << quint32(png.size()) << offset;
        offset += quint32(png.size());
    }
    for (const auto& [size, png] : images)
        out.writeRawData(png.constData(), int(png.size()));
    return out.status() == QDataStream::Ok;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 6) {
        std::fprintf(stderr, "Usage: %s <icon.svg> <icon-small.svg> <max small size> <out dir> <size>...\n", argv[0]);
        return 2;
    }
    // Not "small": windows.h defines that as a macro
    QSvgRenderer detailed(QString::fromLocal8Bit(argv[1]));
    QSvgRenderer simple(QString::fromLocal8Bit(argv[2]));
    if (!detailed.isValid() || !simple.isValid()) {
        std::fprintf(stderr, "iconrender: cannot load %s\n", detailed.isValid() ? argv[2] : argv[1]);
        return 1;
    }
    const int maxSmall = std::atoi(argv[3]);
    const QDir outDir(QString::fromLocal8Bit(argv[4]));
    outDir.mkpath(".");

    std::vector<std::pair<int, QByteArray>> images;
    for (int i = 5; i < argc; ++i) {
        const int size = std::atoi(argv[i]);
        const QByteArray png = renderPng(size <= maxSmall ? simple : detailed, size);
        QFile file(outDir.filePath(QString("replayer-%1.png").arg(size)));
        if (!file.open(QIODevice::WriteOnly) || file.write(png) != png.size()) {
            std::fprintf(stderr, "iconrender: cannot write %s\n", qPrintable(file.fileName()));
            return 1;
        }
        images.emplace_back(size, png);
    }
    if (!writeIco(outDir.filePath("replayer.ico"), images)) {
        std::fprintf(stderr, "iconrender: cannot write replayer.ico\n");
        return 1;
    }
    return 0;
}
