#include "map_database.h"
#include <sstream>
#include "UnitTestsFramework.h"


void MapDataBase::AddRenderSettings(const std::map<std::string, Json::Node>& settings_map) {
    width_ = settings_map.at("width").AsDouble();
    height_ = settings_map.at("height").AsDouble();
    padding_ = settings_map.at("padding").AsDouble();
    stop_radius_ = settings_map.at("stop_radius").AsDouble();
    line_width_ = settings_map.at("line_width").AsDouble();
    stop_label_font_size_ = settings_map.at("stop_label_font_size").AsInt();

    const auto offset = settings_map.at("stop_label_offset").AsArray();
    stop_label_offset_ = Svg::Point{offset[0].AsDouble(), offset[1].AsDouble()};

    underlayer_width_ = settings_map.at("underlayer_width").AsDouble();

    underlayer_color_ = get_color_from_json(
            settings_map.at("underlayer_color"));

    for (const auto& json : settings_map.at("color_palette").AsArray())
        color_palette_.push_back(get_color_from_json(json));

    bus_label_font_size_ = settings_map.at("bus_label_font_size").AsInt();
    const auto bus_offset = settings_map.at("bus_label_offset").AsArray();
    bus_label_offset_ = Svg::Point{bus_offset[0].AsDouble(), bus_offset[1].AsDouble()};

    for (const auto& layer : settings_map.at("layers").AsArray()) {
        layers_list_.push_back(layer.AsString());
    }
}


Svg::Color MapDataBase::get_color_from_json(const Json::Node& json) {
    try {
        return Svg::Color(json.AsString());
    }
    catch (std::bad_variant_access&) {
        const auto& color_settings = json.AsArray();
        const size_t size = color_settings.size();

        if (size == 3)
        {
            return Svg::Color(Svg::Rgb{
                static_cast<int>(color_settings[0].AsInt()),
                static_cast<int>(color_settings[1].AsInt()),
                static_cast<int>(color_settings[2].AsInt())
            });
        }

        return Svg::Color(Svg::Rgba{
                static_cast<int>(color_settings[0].AsInt()),
                static_cast<int>(color_settings[1].AsInt()),
                static_cast<int>(color_settings[2].AsInt()),
                color_settings[3].AsDouble()
        });
    }
}


Svg::Point MapDataBase::compute_point_from_coord(const GroundPoint& coord) const {
    double width_zoom_coef;
    double height_zoom_coef;
    double zoom_coef;

    if (max_ground_point_.GetLongitude() - min_ground_point_.GetLongitude() == 0) {
        if (max_ground_point_.GetLatitude() - min_ground_point_.GetLatitude() == 0) {
            zoom_coef = 0;
        }
        else {
            height_zoom_coef =
                    (height_ - 2 * padding_) /
                    (max_ground_point_.GetLatitude() - min_ground_point_.GetLatitude());
            zoom_coef = height_zoom_coef;
        }
    }
    else {
        width_zoom_coef =
                (width_ - 2 * padding_) /
                (max_ground_point_.GetLongitude() - min_ground_point_.GetLongitude());

        if (max_ground_point_.GetLatitude() - min_ground_point_.GetLatitude() == 0) {
            zoom_coef = width_zoom_coef;
        }
        else {
            height_zoom_coef =
                    (height_ - 2 * padding_) /
                    (max_ground_point_.GetLatitude() - min_ground_point_.GetLatitude());
            zoom_coef = std::min(width_zoom_coef, height_zoom_coef);
        }
    }

    return Svg::Point{
        (coord.GetLongitude() - min_ground_point_.GetLongitude()) *zoom_coef + padding_,
        (max_ground_point_.GetLatitude() - coord.GetLatitude()) * zoom_coef + padding_
    };
}


std::string MapDataBase::GetMapResponse() const {
    std::ostringstream out;
    out.precision(16);
    svg->Render(out);

    return out.str();
}


void MapDataBase::BuildMap(
        const std::shared_ptr<BusStopsDataBase>& stops_base,
        const std::shared_ptr<RoutesDataBase>& buses_base) {

    build_layers(stops_base, buses_base);
    build_svg();
}


void MapDataBase::build_layers(
        const std::shared_ptr<BusStopsDataBase>& stops_base,
        const std::shared_ptr<RoutesDataBase>& buses_base
        ) {
    size_t palette_index = 0;
    for (const auto& bus_name : buses_base->GetRouteNames()) {
        build_line(bus_name, stops_base, buses_base, palette_index);
        ++palette_index;
    }

    recompute_circles_map(stops_base);

    build_end_stops();
    build_circles();
    build_texts();
}


void MapDataBase::build_svg() {
    for (const auto& layer_name : layers_list_) {
        layers_object_->BuildLayer(layer_name, svg);
    }
}


void MapDataBase::build_line(
        const std::string& bus_name,
        const std::shared_ptr<BusStopsDataBase>& stops_base,
        const std::shared_ptr<RoutesDataBase>& buses_base,
        size_t index
        ) {
    index %= color_palette_.size();

    Svg::Polyline polyline;
    polyline.SetStrokeColor(color_palette_[index])
    .SetStrokeWidth(line_width_)
    .SetStrokeLineCap("round")
    .SetStrokeLineJoin("round");

    for (const auto& stop : buses_base->GetRouteInfo(bus_name)->GetStops()) {
        const Svg::Point stop_point =
                compute_point_from_coord(stops_base->GetStopStats(stop)->GetCoordinate());

        polyline.AddPoint(stop_point);

        update_circles_map(stop, stop_point);
    }
    update_end_stops_map(bus_name, stops_base, buses_base, index);

    layers_object_->AddBusLine(polyline);
}


void MapDataBase::update_circles_map(
        const std::string& stop_name,
        const Svg::Point& stop_point) {
    const auto finder = circles_map_.find(stop_name);

    if (finder == circles_map_.end()) {
        circles_map_[stop_name] = Svg::Circle()
                .SetCenter(stop_point)
                .SetRadius(stop_radius_)
                .SetFillColor("white");
    }
}


void MapDataBase::update_end_stops_map(
        const std::string& bus_name,
        const std::shared_ptr<BusStopsDataBase>& stops_base,
        const std::shared_ptr<RoutesDataBase>& buses_base,
        size_t palette_index
                ) {
    const auto end_stops = buses_base->GetRouteInfo(bus_name)->GetEndStops();

    for (const auto& stop : end_stops) {
        bus_to_end_stops_[bus_name].push_back(
                std::make_pair(
                        compute_point_from_coord(
                                stops_base->GetStopStats(stop)->GetCoordinate()
                        ),
                        palette_index
                )
        );
    }
}


void MapDataBase::recompute_circles_map(
        const std::shared_ptr<BusStopsDataBase>& stops_base
        ) {
    for (const auto& stop_pair : stops_base->GetStopsMap()) {
        const Svg::Point stop_point =
                compute_point_from_coord(stop_pair.second->GetCoordinate());

        update_circles_map(stop_pair.first, stop_point);
    }
}


void MapDataBase::build_end_stops() {
    for (const auto& bus_pair : bus_to_end_stops_) {
        for (const auto& end_stop : bus_pair.second) {
            Svg::Text text;
            text.SetPoint(end_stop.first)
            .SetOffset(bus_label_offset_)
            .SetFontSize(bus_label_font_size_)
            .SetFontFamily("Verdana")
            .SetFontWeight("bold")
            .SetData(bus_pair.first);

            Svg::Text substrate_text = text;
            substrate_text.SetFillColor(underlayer_color_)
            .SetStrokeColor(underlayer_color_)
            .SetStrokeWidth(underlayer_width_)
            .SetStrokeLineCap("round")
            .SetStrokeLineJoin("round");

            text.SetFillColor(color_palette_[end_stop.second]);

            layers_object_->AddBusLabel(substrate_text);
            layers_object_->AddBusLabel(text);
        }
    }
}


void MapDataBase::build_circles() {
    for (const auto& circle_pair : circles_map_) {
        layers_object_->AddStopPoint(circle_pair.second);
    }
}


void MapDataBase::build_texts() {
    for (const auto& circle_pair : circles_map_) {
        Svg::Text text;
        text.SetPoint(circle_pair.second.GetCenter())
        .SetOffset(stop_label_offset_)
        .SetFontSize(stop_label_font_size_)
        .SetFontFamily("Verdana")
        .SetData(circle_pair.first);

        Svg::Text substrate_text = text;
        substrate_text.SetFillColor(underlayer_color_)
        .SetStrokeColor(underlayer_color_)
        .SetStrokeWidth(underlayer_width_)
        .SetStrokeLineCap("round")
        .SetStrokeLineJoin("round");

        text.SetFillColor("black");

        layers_object_->AddStopLabel(substrate_text);
        layers_object_->AddStopLabel(text);
    }
}


void TestMapDataBase()
{
    std::istringstream input(R"({ "render_settings": {
        "width": 1200,
        "height": 1200,
        "padding": 50,
        "stop_radius": 5,
        "line_width": 14,
        "bus_label_font_size": 20,
        "bus_label_offset": [
            7,
            15
        ],
        "stop_label_font_size": 20,
        "stop_label_offset": [
            7,
            -3
        ],
        "underlayer_color": [
            255,
            255,
            255,
            0.85
        ],
        "underlayer_width": 3,
        "color_palette": [
            "green",
            [
                255,
                160,
                0
            ],
            "red",
            [
                255,
                255,
                255,
                0.85
            ]
        ],
        "layers": [
            "bus_lines",
            "stop_points",
            "bus_labels"
        ]
    } })");

    const auto json_input = Json::Load(input).GetRoot();

    MapDataBase map_database;

    map_database.AddRenderSettings(
            json_input.AsMap().at("render_settings").AsMap());

    ASSERT(std::abs(map_database.width_ - 1200) < 0.0001)
    ASSERT(std::abs(map_database.height_ - 1200) < 0.0001)
    ASSERT(std::abs(map_database.padding_ - 50) < 0.0001)
    ASSERT(std::abs(map_database.stop_radius_ - 5) < 0.0001)
    ASSERT(std::abs(map_database.height_ - 1200) < 0.0001)
    ASSERT(std::abs(map_database.line_width_ - 14) < 0.0001)
    ASSERT_EQUAL(map_database.bus_label_font_size_, 20)
    ASSERT(std::abs(map_database.bus_label_offset_.x - 7) < 0.0001)
    ASSERT(std::abs(map_database.bus_label_offset_.y - 15) < 0.0001)
    ASSERT_EQUAL(map_database.stop_label_font_size_, 20)
    ASSERT_EQUAL(map_database.underlayer_width_, 3)
    ASSERT(std::abs(map_database.stop_label_offset_.x - 7) < 0.0001)
    ASSERT(std::abs(map_database.stop_label_offset_.y + 3) < 0.0001)

    ASSERT_EQUAL(std::get<Svg::Rgba>(*map_database.underlayer_color_.GetColor()).red, 255)
    ASSERT_EQUAL(std::get<Svg::Rgba>(*map_database.underlayer_color_.GetColor()).green, 255)
    ASSERT_EQUAL(std::get<Svg::Rgba>(*map_database.underlayer_color_.GetColor()).blue, 255)
    ASSERT(std::abs(std::get<Svg::Rgba>(*map_database.underlayer_color_.GetColor()).alpha - 0.85) < 0.0001)

    ASSERT_EQUAL(std::get<std::string>(*map_database.color_palette_[0].GetColor()), "green")
    ASSERT_EQUAL(std::get<std::string>(*map_database.color_palette_[2].GetColor()), "red")

    ASSERT_EQUAL(std::get<Svg::Rgb>(*map_database.color_palette_[1].GetColor()).red, 255)
    ASSERT_EQUAL(std::get<Svg::Rgb>(*map_database.color_palette_[1].GetColor()).green, 160)
    ASSERT_EQUAL(std::get<Svg::Rgb>(*map_database.color_palette_[1].GetColor()).blue, 0)

    ASSERT_EQUAL(std::get<Svg::Rgba>(*map_database.color_palette_[3].GetColor()).red, 255)
    ASSERT_EQUAL(std::get<Svg::Rgba>(*map_database.color_palette_[3].GetColor()).green, 255)
    ASSERT_EQUAL(std::get<Svg::Rgba>(*map_database.color_palette_[3].GetColor()).blue, 255)
    ASSERT(std::abs(std::get<Svg::Rgba>(*map_database.color_palette_[3].GetColor()).alpha - 0.85) < 0.0001)

    ASSERT_EQUAL(map_database.layers_list_[0], "bus_lines")
    ASSERT_EQUAL(map_database.layers_list_[1], "stop_points")
    ASSERT_EQUAL(map_database.layers_list_[2], "bus_labels")
}