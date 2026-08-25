#include "passwordgeneratorwindow.hpp"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "appearancesettings.hpp"
#include "widgets/copylabel.hpp"

namespace {

constexpr int kDefaultLength = 20;

const QString kUppercase = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
const QString kLowercase = QStringLiteral("abcdefghijklmnopqrstuvwxyz");
const QString kNumbers = QStringLiteral("0123456789");
const QString kSymbols = QStringLiteral("!@#$%^&*()-_=+[]{};:,.?/\\|~`");

QString extendedAsciiCharacters() {
    QString result;
    // Visible Latin-1 supplement characters. C1 controls, non-breaking
    // space and soft hyphen are intentionally excluded because they
    // create passwords that look different from what was copied.
    for (ushort code = 0x00A1; code <= 0x00FF; ++code)
        if (code != 0x00AD)
            result.append(QChar(code));
    return result;
}

class GeneratorCheckBox : public QCheckBox {
public:
    using QCheckBox::QCheckBox;

    QSize sizeHint() const override {
        return {18 + 8 + fontMetrics().horizontalAdvance(text()), 26};
    }

protected:
    void paintEvent(QPaintEvent*) override {
        const auto& palette = appearancesettings::palette();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF box(1, 5, 16, 16);
        painter.setPen(QPen(isChecked() ? appearancesettings::accentColor()
                                       : palette.borderStrong,
                            1));
        painter.setBrush(isChecked() ? appearancesettings::accentColor()
                                     : palette.input);
        painter.drawRoundedRect(box, 5, 5);
        if (isChecked()) {
            QPen tick(appearancesettings::accentTextColor(), 1.8);
            tick.setCapStyle(Qt::RoundCap);
            tick.setJoinStyle(Qt::RoundJoin);
            painter.setPen(tick);
            QPainterPath path;
            path.moveTo(5, 13);
            path.lineTo(8, 16);
            path.lineTo(14, 9.5);
            painter.drawPath(path);
        }
        painter.setPen(palette.ink);
        painter.drawText(rect().adjusted(25, 0, 0, 0),
                         Qt::AlignLeft | Qt::AlignVCenter, text());
    }
};

}  // namespace

class StrengthBar : public QWidget {
public:
    explicit StrengthBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(10);
    }

    void setScore(int score) {
        score_ = qBound(0, score, 100);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        constexpr int kGap = 4;
        const int segmentWidth = (width() - 4 * kGap) / 5;
        const int active = score_ == 0 ? 0 : qBound(1, (score_ + 19) / 20, 5);
        const QColor colors[] = {QColor(0xFF, 0x45, 0x3A), QColor(0xFF, 0x9F, 0x0A),
                                 QColor(0xFF, 0xD6, 0x0A), QColor(0x34, 0xC7, 0x59),
                                 QColor(0x0A, 0x8F, 0x3D)};
        for (int i = 0; i < 5; ++i) {
            const int x = i * (segmentWidth + kGap);
            const int width = i == 4 ? this->width() - x : segmentWidth;
            painter.setPen(Qt::NoPen);
            painter.setBrush(i < active ? colors[i] : appearancesettings::palette().border);
            painter.drawRoundedRect(QRectF(x, 1, width, height() - 2), 4, 4);
        }
    }

private:
    int score_ = 0;
};

PasswordGeneratorWindow::PasswordGeneratorWindow(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("passwordGeneratorWindow"));
    setWindowTitle(tr("Password Generator"));
    setWindowFlag(Qt::Window, true);
    setWindowModality(Qt::NonModal);
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(26, 24, 26, 24);
    layout->setSpacing(18);

    auto* subtitle = new QLabel(tr("Generate a strong password using the character sets below."));
    subtitle->setObjectName(QStringLiteral("generatorSubtitle"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    auto* resultCard = new QFrame;
    resultCard->setObjectName(QStringLiteral("generatorCard"));
    auto* resultLayout = new QVBoxLayout(resultCard);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    resultLayout->setSpacing(12);

    password_ = new CopyLabel;
    password_->setObjectName(QStringLiteral("generatorPassword"));
    password_->setContentAlignment(Qt::AlignCenter);
    password_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    password_->setFixedHeight(42);
    QFont passwordFont = password_->font();
    passwordFont.setPixelSize(14);
    password_->setFont(passwordFont);
    resultLayout->addWidget(password_);

    auto* strengthHeader = new QHBoxLayout;
    auto* strengthTitle = new QLabel(tr("Strength"));
    strengthTitle->setObjectName(QStringLiteral("generatorLabel"));
    strengthLabel_ = new QLabel;
    strengthLabel_->setObjectName(QStringLiteral("generatorStrength"));
    strengthHeader->addWidget(strengthTitle);
    strengthHeader->addStretch(1);
    strengthHeader->addWidget(strengthLabel_);
    resultLayout->addLayout(strengthHeader);
    strengthBar_ = new StrengthBar;
    resultLayout->addWidget(strengthBar_);

    auto* actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    generateButton_ = new QPushButton(tr("Generate"));
    generateButton_->setObjectName(QStringLiteral("generatorButton"));
    generateButton_->setCursor(Qt::PointingHandCursor);
    generateButton_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    generateButton_->setIconSize(QSize(18, 18));
    const auto tintDiceGlyph = [this] {
        generateButton_->setIcon(appearancesettings::tintedMenuIcon(
            QStringLiteral("dice"), appearancesettings::accentTextColor()));
    };
    tintDiceGlyph();
    connect(appearancesettings::notifier(), &appearancesettings::Notifier::changed,
            this, tintDiceGlyph);
    actions->addWidget(generateButton_);
    resultLayout->addLayout(actions);
    layout->addWidget(resultCard);

    auto* optionsCard = new QFrame;
    optionsCard->setObjectName(QStringLiteral("generatorCard"));
    auto* optionsLayout = new QVBoxLayout(optionsCard);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(10);

    auto* lengthHeader = new QHBoxLayout;
    auto* lengthTitle = new QLabel(tr("Length"));
    lengthTitle->setObjectName(QStringLiteral("generatorLabel"));
    lengthValue_ = new QLabel(QString::number(kDefaultLength));
    lengthValue_->setObjectName(QStringLiteral("generatorLengthValue"));
    lengthHeader->addWidget(lengthTitle);
    lengthHeader->addStretch(1);
    lengthHeader->addWidget(lengthValue_);
    optionsLayout->addLayout(lengthHeader);

    lengthSlider_ = new QSlider(Qt::Horizontal);
    lengthSlider_->setObjectName(QStringLiteral("generatorLength"));
    lengthSlider_->setRange(1, 128);
    lengthSlider_->setValue(kDefaultLength);
    optionsLayout->addWidget(lengthSlider_);
    auto* lengthRange = new QHBoxLayout;
    auto* minimumLength = new QLabel(QStringLiteral("1"));
    auto* maximumLength = new QLabel(QStringLiteral("128"));
    minimumLength->setObjectName(QStringLiteral("generatorRange"));
    maximumLength->setObjectName(QStringLiteral("generatorRange"));
    lengthRange->addWidget(minimumLength);
    lengthRange->addStretch(1);
    lengthRange->addWidget(maximumLength);
    optionsLayout->addLayout(lengthRange);
    optionsLayout->addSpacing(6);

    auto* setsTitle = new QLabel(tr("Character sets"));
    setsTitle->setObjectName(QStringLiteral("generatorLabel"));
    optionsLayout->addWidget(setsTitle);

    uppercase_ = new GeneratorCheckBox(tr("Uppercase letters"));
    lowercase_ = new GeneratorCheckBox(tr("Lowercase letters"));
    numbers_ = new GeneratorCheckBox(tr("Numbers"));
    symbols_ = new GeneratorCheckBox(tr("Special characters"));
    extendedAscii_ = new GeneratorCheckBox(tr("Extended ASCII"));
    uppercase_->setChecked(true);
    lowercase_->setChecked(true);
    numbers_->setChecked(true);
    symbols_->setChecked(true);
    for (QCheckBox* checkbox : {uppercase_, lowercase_, numbers_, symbols_, extendedAscii_}) {
        checkbox->setCursor(Qt::PointingHandCursor);
        optionsLayout->addWidget(checkbox);
        connect(checkbox, &QCheckBox::toggled, this, [this] {
            updateAvailability();
            if (generateButton_->isEnabled())
                generatePassword();
        });
    }
    layout->addWidget(optionsCard);

    connect(lengthSlider_, &QSlider::valueChanged, this, [this](int length) {
        lengthValue_->setText(QString::number(length));
        generatePassword();
    });
    connect(generateButton_, &QPushButton::clicked, this,
            &PasswordGeneratorWindow::generatePassword);
    generatePassword();
    adjustSize();
    setFixedSize(sizeHint());
}

void PasswordGeneratorWindow::updateAvailability() {
    const bool available = uppercase_->isChecked() || lowercase_->isChecked() ||
                           numbers_->isChecked() || symbols_->isChecked() ||
                           extendedAscii_->isChecked();
    generateButton_->setEnabled(available);
    if (!available) {
        password_->setText({});
        strengthBar_->setScore(0);
        strengthLabel_->setText(tr("Select a character set"));
    }
}

void PasswordGeneratorWindow::generatePassword() {
    QList<QString> groups;
    if (uppercase_->isChecked()) groups.append(kUppercase);
    if (lowercase_->isChecked()) groups.append(kLowercase);
    if (numbers_->isChecked()) groups.append(kNumbers);
    if (symbols_->isChecked()) groups.append(kSymbols);
    if (extendedAscii_->isChecked()) groups.append(extendedAsciiCharacters());
    if (groups.isEmpty()) {
        updateAvailability();
        return;
    }

    QString pool;
    for (const QString& group : groups)
        pool += group;

    const int length = lengthSlider_->value();
    QString result;
    result.reserve(length);
    QRandomGenerator* random = QRandomGenerator::system();

    // When possible, guarantee at least one character from every
    // requested class before filling the remaining positions.
    if (length >= groups.size())
        for (const QString& group : groups)
            result.append(group.at(random->bounded(group.size())));
    while (result.size() < length)
        result.append(pool.at(random->bounded(pool.size())));
    for (int i = result.size() - 1; i > 0; --i) {
        const int other = random->bounded(i + 1);
        const QChar value = result.at(i);
        result[i] = result.at(other);
        result[other] = value;
    }

    password_->setText(result);
    updateStrength();
}

void PasswordGeneratorWindow::updateStrength() {
    const QString password = password_->text();
    if (password.isEmpty()) {
        strengthBar_->setScore(0);
        strengthLabel_->clear();
        return;
    }

    int poolSize = 0;
    bool upper = false;
    bool lower = false;
    bool number = false;
    bool symbol = false;
    bool extended = false;
    for (QChar character : password) {
        const ushort code = character.unicode();
        upper |= character.isUpper() && code < 128;
        lower |= character.isLower() && code < 128;
        number |= character.isDigit() && code < 128;
        extended |= code >= 128;
        symbol |= code < 128 && !character.isLetterOrNumber();
    }
    if (upper) poolSize += kUppercase.size();
    if (lower) poolSize += kLowercase.size();
    if (number) poolSize += kNumbers.size();
    if (symbol) poolSize += kSymbols.size();
    if (extended) poolSize += extendedAsciiCharacters().size();

    const qreal entropy = password.size() * std::log2(qMax(1, poolSize));
    int score = qBound(1, qRound(entropy / 1.28), 100);
    if (password.size() < 4)
        score = qMin(score, 19);
    else if (password.size() < 8)
        score = qMin(score, 39);
    strengthBar_->setScore(score);

    if (score <= 20)
        strengthLabel_->setText(tr("Very weak"));
    else if (score <= 40)
        strengthLabel_->setText(tr("Weak"));
    else if (score <= 60)
        strengthLabel_->setText(tr("Fair"));
    else if (score <= 80)
        strengthLabel_->setText(tr("Strong"));
    else
        strengthLabel_->setText(tr("Very strong"));
}
