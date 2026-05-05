#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <vector>
#include "decoder.hpp"

namespace py = pybind11;

// ------------------------- Level0 -------------------------
py::dtype level0_dtype() {
    py::list fields;
    fields.append(py::make_tuple("clk", py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("V2",  py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("H2",  py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("TG",  py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("OG",  py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("SW",  py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("DG",  py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("RU",  py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("RD",  py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("val", py::dtype::of<int32_t>()));
    py::module_ np = py::module_::import("numpy");
    return np.attr("dtype")(fields).cast<py::dtype>();
}

// ------------------------- Level1 -------------------------
py::dtype level1_dtype() {
    py::list fields;
    fields.append(py::make_tuple("pix",  py::dtype::of<uint32_t>()));
    fields.append(py::make_tuple("line", py::dtype::of<uint32_t>()));
    fields.append(py::make_tuple("skip", py::dtype::of<uint16_t>()));
    fields.append(py::make_tuple("ramp", py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("idx",  py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("n",    py::dtype::of<uint32_t>()));
    fields.append(py::make_tuple("sum",  py::dtype::of<int32_t>()));
    py::module_ np = py::module_::import("numpy");
    return np.attr("dtype")(fields).cast<py::dtype>();
}

// ------------------------- Level2 -------------------------
py::dtype level2_dtype() {
    py::list fields;
    fields.append(py::make_tuple("pix",      py::dtype::of<uint32_t>()));
    fields.append(py::make_tuple("line",     py::dtype::of<uint32_t>()));
    fields.append(py::make_tuple("cin",      py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("idx",      py::dtype::of<uint8_t>()));
    fields.append(py::make_tuple("cds_n",    py::dtype::of<uint32_t>()));
    fields.append(py::make_tuple("cds_sum",  py::dtype::of<int64_t>()));
    fields.append(py::make_tuple("cds_sum2", py::dtype::of<int64_t>()));
    fields.append(py::make_tuple("cts_n",    py::dtype::of<uint32_t>()));
    fields.append(py::make_tuple("cts_sum",  py::dtype::of<int64_t>()));
    fields.append(py::make_tuple("cts_sum2", py::dtype::of<int64_t>()));
    py::module_ np = py::module_::import("numpy");
    return np.attr("dtype")(fields).cast<py::dtype>();
}

// ------------------------- Generic zero-copy -------------------------
template <typename T>
py::object dataframe_from_vector(std::vector<T>& data, const py::dtype& dtype, py::object base) {
    py::array arr(
        dtype,
        { data.size() },
        { sizeof(T) },
        data.data(),
        base   // ties lifetime to the owning object
    );

    py::module pandas = py::module::import("pandas");
    return pandas.attr("DataFrame")(arr);
}

PYBIND11_MODULE(decoder, m){
    py::class_<BinaryDecoder>(m, "Decoder")
         // short-form: Decoder(binfile [, debug, write_log]) -> from_bin
         // auto-detects a sibling <stem>.meta file and uses it if present.
        .def(py::init([](py::object binfile,
                          uint32_t debug,
                          bool write_log) -> std::unique_ptr<BinaryDecoder> {
               py::module_ os = py::module_::import("os");
               std::string bin_path = os.attr("fspath")(binfile).cast<std::string>();
               return std::make_unique<BinaryDecoder>(
                   std::move(BinaryDecoder::from_bin(bin_path, debug, write_log))
               );
             }),
             py::arg("binfile"),
             py::arg("debug") = 0,
             py::arg("write_log") = false
         )

         // full-form: explicit geometry, no .meta lookup
        .def(py::init<std::string,uint32_t,bool,uint32_t,uint32_t,uint32_t,uint32_t>(),
             py::arg("fname"),
             py::arg("debug")=0,
             py::arg("write_log")=false,
             py::arg("nrow")=0,
             py::arg("ncol")=0,
             py::arg("ndcm")=1,
             py::arg("nint")=0)

         // alternate constructor from (jsonfile, binfile, ...)
        .def(py::init([](py::object jsonfile,
                          py::object binfile,
                          uint32_t debug,
                          bool write_log) -> std::unique_ptr<BinaryDecoder> {

               py::module_ os = py::module_::import("os");
               std::string json_path = os.attr("fspath")(jsonfile).cast<std::string>();
               std::string bin_path  = os.attr("fspath")(binfile).cast<std::string>();

               return std::make_unique<BinaryDecoder>(
                   std::move(BinaryDecoder::from_meta(json_path, bin_path, debug, write_log))
               );
             }),
              py::arg("jsonfile"),
              py::arg("binfile"),
              py::arg("debug") = 0,
              py::arg("write_log")=false
         )

        .def_property_readonly("df0_view", [](BinaryDecoder& self) {
            return dataframe_from_vector(
                self.frame0, level0_dtype(), py::cast(&self)
            );
        })
        .def_property_readonly("df1_view", [](BinaryDecoder& self) {
            return dataframe_from_vector(
                self.frame1, level1_dtype(), py::cast(&self)
            );
        })
        .def_property_readonly("df2_view", [](BinaryDecoder& self) {
            return dataframe_from_vector(
                self.frame2, level2_dtype(), py::cast(&self)
            );
        })
        // Expose NumPy 1D views (zero-copy)
        .def_property_readonly("cds_skp_view", [](BinaryDecoder& self) {
            return py::array_t<double>(
                self.v_cds.size(), self.v_cds.data(), py::cast(&self)
            );
        })
        .def_property_readonly("cds_avg_view", [](BinaryDecoder& self) {
            return py::array_t<double>(
                self.v_cds_avg.size(), self.v_cds_avg.data(), py::cast(&self)
            );
        })
        .def_property_readonly("cds_var_view", [](BinaryDecoder& self) {
            return py::array_t<double>(
                self.v_cds_var.size(), self.v_cds_var.data(), py::cast(&self)
            );
        })
        .def_property_readonly("cts_skp_view", [](BinaryDecoder& self) {
            return py::array_t<double>(
                self.v_cts.size(), self.v_cts.data(), py::cast(&self)
            );
        })
        .def_property_readonly("cts_avg_view", [](BinaryDecoder& self) {
            return py::array_t<double>(
                self.v_cts_avg.size(), self.v_cts_avg.data(), py::cast(&self)
            );
        })
        .def_property_readonly("cts_var_view", [](BinaryDecoder& self) {
            return py::array_t<double>(
                self.v_cts_var.size(), self.v_cts_var.data(), py::cast(&self)
            );
        })

        // book keeping parameters
        .def_readonly("filename", &BinaryDecoder::filename)
        .def_readonly("nrow", &BinaryDecoder::nrow)
        .def_readonly("ncol", &BinaryDecoder::ncol)
        .def_readonly("ndcm", &BinaryDecoder::ndcm)
        .def_readonly("nadc", &BinaryDecoder::nadc)

        // Data from first word after busy
        .def_readonly("info_word", &BinaryDecoder::info_word)
        .def_readonly("RESERVE_BIT", &BinaryDecoder::RESERVE_BIT)
        .def_readonly("ITP_FIRMWARE", &BinaryDecoder::ITP_FIRMWARE)
        .def_readonly("LVL", &BinaryDecoder::LVL)
        .def_readonly("LVL1_SIZE", &BinaryDecoder::LVL1_SIZE)
        .def_readonly("LVL2_SIZE", &BinaryDecoder::LVL2_SIZE)
        .def_readonly("LV_FREQ", &BinaryDecoder::LV_FREQ)

        // Error codes
        .def_readonly("error_acc", &BinaryDecoder::error_acc)
        .def_readonly("error_idx", &BinaryDecoder::error_idx)
        .def_readonly("error_cin", &BinaryDecoder::error_cin)
        .def_readonly("error_nadc", &BinaryDecoder::error_nadc)
        
        // internal counter of EPIX and LINE and CPONG
        .def_readonly("cclki", &BinaryDecoder::cclki)
        .def_readonly("cclke", &BinaryDecoder::cclke)
        .def_readonly("cepix", &BinaryDecoder::cepix)
        .def_readonly("cline", &BinaryDecoder::cline)
        .def_readonly("cpong", &BinaryDecoder::cpong)
        .def_readonly("start", &BinaryDecoder::cstart)
        .def_readonly("busy", &BinaryDecoder::cbusy)
        .def_readonly("end", &BinaryDecoder::cend);
}

