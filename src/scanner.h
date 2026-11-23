#ifndef SCANNER_H
#define SCANNER_H

#include <QApplication>

class Scanner final : public QApplication
{
    Q_OBJECT
public:
    Scanner(int& argc, char **argv);

signals:
    void StyleChanged();

public slots:
    static void LoadTheme(const std::string& theme);

protected:
    bool event(QEvent *) override;

private:
    std::string theme_{ "auto" };
};

inline Scanner *App() { return static_cast<Scanner *>(qApp); }

#endif //! SCANNER_H