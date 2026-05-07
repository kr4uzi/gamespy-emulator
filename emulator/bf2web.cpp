#include "bf2web.h"
using namespace gamespy;

template<>
void gamespy::bf2web::response::AppendElement(const std::string_view& elem)
{
	length += elem.length();
	str += elem;
	str += '\t';
}
