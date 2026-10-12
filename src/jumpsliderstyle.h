#ifndef JUMPSLIDERSTYLE_H
#define JUMPSLIDERSTYLE_H

#include <QProxyStyle>

// Clicking the slider groove jumps straight to that position instead of
// paging towards it.
class JumpSliderStyle : public QProxyStyle {
public:
    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returnData) const override {
        if (hint == SH_Slider_AbsoluteSetButtons)
            return Qt::LeftButton | Qt::MiddleButton;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

#endif
