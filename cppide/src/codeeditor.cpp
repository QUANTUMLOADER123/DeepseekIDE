#include "codeeditor.h"
#include <QPainter>
#include <QTextBlock>

class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), editor_(editor) {}
    QSize sizeHint() const override { return QSize(editor_->lineNumberAreaWidth(), 0); }
protected:
    void paintEvent(QPaintEvent* e) override { editor_->lineNumberAreaPaintEvent(e); }
private:
    CodeEditor* editor_;
};

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent) {
    lineNumberArea_ = new LineNumberArea(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::textChanged, this, &CodeEditor::onTextChanged);
    updateLineNumberAreaWidth(0);
    highlightCurrentLine();
    QFont f(QStringLiteral("Consolas")); f.setStyleHint(QFont::Monospace); f.setPointSize(10);
    setFont(f);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
}

void CodeEditor::setLanguageByExt(const QString& ext) {
    auto* h = new SimpleHighlighter(document());
    h->setLanguage(ext);
}

int CodeEditor::lineNumberAreaWidth() {
    int digits = 1, max = qMax(1, blockCount());
    while (max >= 10) { max /= 10; ++digits; }
    return 8 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth(int) {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

void CodeEditor::updateLineNumberArea(const QRect& rect, int dy) {
    if (dy) lineNumberArea_->scroll(0, dy);
    else lineNumberArea_->update(0, rect.y(), lineNumberArea_->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateLineNumberAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent* e) {
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extra;
    if (!isReadOnly()) {
        QTextEdit::ExtraSelection sel;
        QColor c = palette().color(QPalette::Base).lighter(112);
        sel.format.setBackground(c);
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor(); sel.cursor.clearSelection();
        extra.append(sel);
    }
    setExtraSelections(extra);
}

void CodeEditor::onTextChanged() {
    // модифицированность смотрится через document()->isModified()
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent* e) {
    QPainter p(lineNumberArea_);
    p.fillRect(e->rect(), palette().color(QPalette::Window));
    QTextBlock block = firstVisibleBlock();
    int bn = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const QColor fg = palette().color(QPalette::Disabled, QPalette::Text);
    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            p.setPen(fg);
            p.drawText(0, top, lineNumberArea_->width() - 4, fontMetrics().height(),
                       Qt::AlignRight, QString::number(bn + 1));
        }
        block = block.next(); top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++bn;
    }
}

// ---------------- подсветка ----------------
SimpleHighlighter::SimpleHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
    kwFmt_.setForeground(QColor(197, 134, 192));
    typeFmt_.setForeground(QColor(86, 156, 214));
    litFmt_.setForeground(QColor(206, 145, 120));
    commentFmt_.setForeground(QColor(106, 153, 85));
    preprocFmt_.setForeground(QColor(175, 175, 175));
    setLanguage(QStringLiteral("cpp"));
}

void SimpleHighlighter::setLanguage(const QString& ext) {
    const QString e = ext.toLower();
    cpp_ = !(e == QStringLiteral("py") || e == QStringLiteral("pyw"));
    rules_.clear();
    auto add = [&](const QString& pat, const QTextCharFormat& fmt, bool cs = false) {
        Rule r; r.pattern = QRegularExpression(pat, cs ? QRegularExpression::NoPatternOption
                                                       : QRegularExpression::CaseInsensitiveOption);
        r.format = fmt; rules_.append(r);
    };
    if (cpp_) {
        add(QStringLiteral("\\b(alignas|alignof|asm|auto|bool|break|case|catch|char|class|const|constexpr|continue|decltype|default|delete|do|double|else|enum|explicit|export|extern|false|float|for|friend|goto|if|inline|int|long|mutable|namespace|new|noexcept|nullptr|operator|private|protected|public|register|requires|return|short|signed|sizeof|static|static_assert|struct|switch|template|this|thread_local|throw|true|try|typedef|typename|union|unsigned|using|virtual|void|volatile|wchar_t|while)\\b"), kwFmt_, true);
        add(QStringLiteral("(^|\\s)#[^\n]*"), preprocFmt_, true);
        add(QStringLiteral("//[^\n]*"), commentFmt_, true);
        add(QStringLiteral("\\b(u?int(8|16|32|64)_t|size_t|string|vector|map|unordered_map|optional|unique_ptr|shared_ptr|QString|QList)\\b"), typeFmt_, true);
        add(QStringLiteral("\"([^\"\\\\]|\\\\.)*\""), litFmt_, true);
        add(QStringLiteral("'([^'\\\\]|\\\\.)?'"), litFmt_, true);
        add(QStringLiteral("\\b\\d+(\\.\\d+)?(u|l|f|ll)?\\b"), litFmt_, true);
    } else {
        add(QStringLiteral("\\b(and|as|assert|async|await|break|class|continue|def|del|elif|else|except|False|finally|for|from|global|if|import|in|is|lambda|None|nonlocal|not|or|pass|raise|return|True|try|while|with|yield|print|len)\\b"), kwFmt_, true);
        add(QStringLiteral("#[^\n]*"), commentFmt_, true);
        add(QStringLiteral("\"([^\"\\\\]|\\\\.)*\""), litFmt_, true);
        add(QStringLiteral("'([^'\\\\]|\\\\.)*'"), litFmt_, true);
        add(QStringLiteral("\\b\\d+(\\.\\d+)?\\b"), litFmt_, true);
    }
    rehighlight();
}

void SimpleHighlighter::highlightBlock(const QString& text) {
    for (const Rule& r : rules_) {
        auto it = r.pattern.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            int start = m.capturedStart(), len = m.capturedLength();
            if (m.lastCapturedIndex() >= 1 && r.format == preprocFmt_) { start = m.capturedStart(1) >= 0 ? m.capturedStart(1) : start; }
            if (len > 0) setFormat(start, len, r.format);
        }
    }
    if (cpp_) {
        // многострочные /* … */
        static const QRegularExpression st(QStringLiteral("/\\*")), en(QStringLiteral("\\*/"));
        int start = 0;
        if (previousBlockState() != 1) start = text.indexOf(st);
        while (start >= 0) {
            auto m = en.match(text, start + 2);
            int end = m.hasMatch() ? m.capturedStart() : text.size();
            int len = end - start + (m.hasMatch() ? 2 : 0);
            setFormat(start, len, commentFmt_);
            setCurrentBlockState(m.hasMatch() ? 0 : 1);
            if (!m.hasMatch()) break;
            start = text.indexOf(st, end + 2);
        }
    }
}
