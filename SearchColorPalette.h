// SearchColorPalette.h - 搜索 / 关键字高亮统一的配色（避免散落多处的颜色定义）
#ifndef SEARCH_COLOR_PALETTE_H
#define SEARCH_COLOR_PALETTE_H

#include <QColor>
#include <Qt>

namespace SearchPalette {

inline const QColor *pool(int *outSize)
{
    static const QColor kPool[] = {
        QColor(255, 182, 193), // light pink
        QColor(173, 216, 230), // light blue
        QColor(144, 238, 144), // light green
        QColor(255, 255, 150), // light yellow
        QColor(255, 160, 122), // light salmon
        QColor(255, 228, 181), // moccasin
        QColor(221, 160, 221), // plum
        QColor(176, 224, 230), // powder blue
        QColor(152, 251, 152), // pale green
        QColor(240, 230, 140)  // khaki
    };
    if (outSize) *outSize = int(sizeof(kPool) / sizeof(kPool[0]));
    return kPool;
}

// 第 0 个黄色、第 1 个绿色、其余按池循环 —— 与历史行为保持一致。
inline QColor colorForIndex(int idx)
{
    if (idx <= 0) return Qt::yellow;
    if (idx == 1) return Qt::green;
    int n = 0;
    const QColor *p = pool(&n);
    return p[(idx - 2) % n];
}

} // namespace SearchPalette

#endif // SEARCH_COLOR_PALETTE_H
