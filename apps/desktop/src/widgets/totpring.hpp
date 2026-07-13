#pragma once

#include <QWidget>

// Circular countdown for the 30-second TOTP window.
class TotpRing : public QWidget {
    Q_OBJECT
public:
    explicit TotpRing(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};
