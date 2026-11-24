#ifndef SCANNER_CSV_H
#define SCANNER_CSV_H

#include <fstream>
#include <string>

class CSV
{
public:
    ~CSV();

    bool open(std::string filename);

    [[nodiscard]] bool opened() const;

    void write(std::string_view str);

    void close();

private:
    std::string  filename_{};
    std::fstream file_{};
};

#endif //! SCANNER_CSV_H
