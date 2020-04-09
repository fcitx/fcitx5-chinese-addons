//
// Copyright (C) 2013~2018 by CSSlayer
// wengxt@gmail.com
//
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; see the file COPYING. If not,
// see <http://www.gnu.org/licenses/>.
//

#include "browserdialog.h"
#include "config.h"
#include "filedownloader.h"
#include "guicommon.h"
#include <QDebug>
#include <QIcon>
#include <QMessageBox>
#include <QTemporaryFile>
#include <QTextCodec>
#include <QUrl>
#include <QUrlQuery>
#include <fcitx-utils/i18n.h>

namespace fcitx {

#ifdef USE_WEBKIT
using WebPageBase = QWebPage;
using WebViewType = QWebView;
#else
using WebPageBase = QWebEnginePage;
using WebViewType = QWebEngineView;
#endif

/*
 * a typical link looks like this.
 * http://download.pinyin.sogou.com/dict/download_cell.php?id=15207&name=%D6%B2%CE%EF%B4%CA%BB%E3%B4%F3%C8%AB%A1%BE%B9%D9%B7%BD%CD%C6%BC%F6%A1%BF
 */

class WebPage : public WebPageBase {
public:
    WebPage(BrowserDialog *dialog) : WebPageBase(dialog), dialog_(dialog) {}

protected:
#ifdef USE_WEBKIT
    bool acceptNavigationRequest(QWebFrame *, const QNetworkRequest &request,
                                 NavigationType) override {
        return dialog_->linkClicked(request.url());
    }
#else
    bool acceptNavigationRequest(const QUrl &url, NavigationType,
                                 bool) override {
        return dialog_->linkClicked(url);
    }
#endif

    WebPageBase *createWindow(WebPageBase::WebWindowType) override {
        return this;
    }

private:
    BrowserDialog *dialog_;
};

BrowserDialog::BrowserDialog(QWidget *parent)
    : QDialog(parent), page_(new WebPage(this)) {
    setupUi(this);
    webView_->setPage(page_);
    setWindowIcon(QIcon::fromTheme("internet-web-browser"));
    setWindowTitle(_("Browse Sogou Cell Dict repository"));

    connect(webView_, &WebViewType::loadProgress, progressBar_,
            &QProgressBar::setValue);
    connect(webView_, &WebViewType::loadStarted, progressBar_,
            &QProgressBar::show);
    connect(webView_, &WebViewType::loadFinished, progressBar_,
            &QProgressBar::hide);
    webView_->load(QUrl(URL_BASE));
}

BrowserDialog::~BrowserDialog() {}

QString BrowserDialog::decodeName(const QByteArray &in) {
    QTextCodec *codec = QTextCodec::codecForName("UTF-8");
    if (!codec) {
        return QString();
    }
    QByteArray out = QByteArray::fromPercentEncoding(in);
    return codec->toUnicode(out);
}

bool BrowserDialog::linkClicked(const QUrl &url) {
    do {
        if (url.host() != DOWNLOAD_HOST_BASE && url.host() != HOST_BASE) {
            break;
        }

        // Now the site seems to have "d/dict/download_cell.php", just to make
        // it works with both.
        if (!url.path().endsWith("/dict/download_cell.php")) {
            break;
        }
        QUrlQuery query(url);
        QString id = query.queryItemValue("id");
        QByteArray name =
            query.queryItemValue("name", QUrl::FullyEncoded).toLatin1();
        QString sname = decodeName(name);

        name_ = sname;
        url_ = url;

        if (!id.isEmpty() && !sname.isEmpty()) {
            accept();
            return false;
        }
    } while (0);

    if (url.host() != HOST_BASE) {
        QMessageBox::information(this, _("Wrong Link"),
                                 _("No browsing outside pinyin.sogou.com, now "
                                   "redirect to home page."));
        webView_->load(QUrl(URL_BASE));
        return false;
    } else {
        return true;
    }
}

} // namespace fcitx
