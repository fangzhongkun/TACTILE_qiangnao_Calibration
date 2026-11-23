#include "csv.h"

CSV::~CSV() { close(); }

bool CSV::open(std::string filename)
{
    close();

    filename_ = std::move(filename);
    file_.open(filename_, std::ios::out);

    return file_.is_open();
}

bool CSV::opened() const { return file_.is_open(); }

void CSV::write(std::string_view str) { file_ << str; }

void CSV::close()
{
    if (file_) file_.close();
}