#include "AboutDialog.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "AlsaTransport.h"
#include "SweetSpot.h"

namespace AboutDialog
{

void show(QWidget* parent)
{
    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QObject::tr("About %1").arg(QApplication::applicationDisplayName()));
    auto* layout = new QVBoxLayout(dialog);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    auto* text = new QLabel(dialog);
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    text->setOpenExternalLinks(true);
    text->setTextInteractionFlags(Qt::TextBrowserInteraction);
    // a fixed width, so the wrapped text's height is exact and the dialog fits it
    text->setFixedWidth(480);
    text->setText(
        QObject::tr(
            "<h2>%1 %2</h2>"
            "<p>Configures the internal DSP mixer of the Steinberg UR44 USB audio interface: the mixes, the "
            "Sweet Spot Morphing Channel Strip, Guitar Amp Classics and REV-X.</p>"
            "<p>Copyright &copy; 2026 Petr Mr&aacute;zek. Released under the Microsoft Public License (Ms-PL).</p>"
            "<h3>Credits</h3>"
            "<p>The UR44, dspMixFx, REV-X, the Sweet Spot Morphing Channel Strip and Guitar Amp Classics are made by "
            "Yamaha Corporation and Steinberg Media Technologies. The UR44's control protocol was reverse "
            "engineered from their <i>TOOLS for UR44</i>, so that the device can be used on Linux.</p>"
            "<p>The 34 built-in Sweet Spot Data presets come from TOOLS for UR44: %3</p>"
            "<p>This project isn't affiliated with or endorsed by Yamaha or Steinberg. Product names are trademarks "
            "of their respective owners.</p>"
            "<p>Built with Qt %4 (running %5) and ALSA %6.</p>")
            .arg(QApplication::applicationDisplayName().toHtmlEscaped(),
                 QApplication::applicationVersion().toHtmlEscaped(),
                 UR44::SweetSpotPreset::builtInNotice().toHtmlEscaped(), QString::fromLatin1(QT_VERSION_STR),
                 QString::fromLatin1(qVersion()), UR44::AlsaTransport::alsaVersion().toHtmlEscaped()));
    layout->addWidget(text);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QPushButton* aboutQt = buttons->addButton(QObject::tr("About Qt"), QDialogButtonBox::ActionRole);
    QObject::connect(aboutQt, &QPushButton::clicked, dialog, [dialog]() { QMessageBox::aboutQt(dialog); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog->open();
}

}
