#pragma once
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QRegularExpression>

class LineNumberArea;

// Редактор с номерами строк (стандартный паттерн Qt Code Editor)
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);
    void lineNumberAreaPaintEvent(QPaintEvent* event);
    int  lineNumberAreaWidth();
    QString path() const { return path_; }
    void    setPath(const QString& p) { path_ = p; }
    bool    modified() const { return document()->isModified(); }
    void    markSaved() { document()->setModified(false); }
    void    setLanguageByExt(const QString& ext);

protected:
    void resizeEvent(QResizeEvent* e) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void highlightCurrentLine();
    void updateLineNumberArea(const QRect& rect, int dy);
    void onTextChanged();

signals:
    void editingFinished();

private:
    QWidget* lineNumberArea_;
    QString  path_;
};

// Простая подсветка C++/Python/JS по расширению
class SimpleHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit SimpleHighlighter(QTextDocument* parent = nullptr);
    void setLanguage(const QString& ext);

protected:
    void highlightBlock(const QString& text) override;

private:
    struct Rule { QRegularExpression pattern; QTextCharFormat format; };
    QVector<Rule> rules_;
    QTextCharFormat kwFmt_, typeFmt_, litFmt_, commentFmt_, preprocFmt_;
    bool cpp_ = true;
};
