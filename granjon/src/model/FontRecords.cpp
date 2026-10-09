#include "model/FontRecords.hpp"

namespace granjon::model {

int Metrics::get(Field f) const {
    switch (f) {
        case Field::Ascender:  return ascender;
        case Field::Descender: return descender;
        case Field::XHeight:   return x_height;
        case Field::CapHeight: return cap_height;
        case Field::Overshoot: return overshoot;
    }
    return 0;
}

void Metrics::set(Field f, int v) {
    switch (f) {
        case Field::Ascender:  ascender = v;   break;
        case Field::Descender: descender = v;  break;
        case Field::XHeight:   x_height = v;   break;
        case Field::CapHeight: cap_height = v; break;
        case Field::Overshoot: overshoot = v;  break;
    }
}

const char* field_name(Metrics::Field f) {
    switch (f) {
        case Metrics::Field::Ascender:  return "Ascender";
        case Metrics::Field::Descender: return "Descender";
        case Metrics::Field::XHeight:   return "x-height";
        case Metrics::Field::CapHeight: return "Cap height";
        case Metrics::Field::Overshoot: return "Overshoot";
    }
    return "?";
}

}  // namespace granjon::model
