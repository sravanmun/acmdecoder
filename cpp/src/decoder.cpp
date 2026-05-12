#include "decoder.hpp"
#include "helper.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>


static constexpr uint32_t MASKU32(int width) { return (1u << width) - 1u; }


static constexpr uint32_t seq_to_adc(uint32_t nint) {
    const uint32_t CLK_FREQ_ADC_MHZ = 15;
    const uint32_t CLK_FREQ_SEQ_MHZ = 100;
    return (static_cast<uint64_t>(nint) * CLK_FREQ_ADC_MHZ
            + CLK_FREQ_SEQ_MHZ - 1) / CLK_FREQ_SEQ_MHZ;
}

//Constructor
BinaryDecoder::BinaryDecoder(std::string fname,
                  uint32_t output_lvl,
                  bool write_log,
                  uint32_t nrow,
                  uint32_t ncol,
                  uint32_t ndcm,
                  uint32_t nint):
        filename(std::move(fname)),
        output_lvl(output_lvl),
        nrow(nrow),
        ncol(ncol),
        ndcm(ndcm),
        nadc(seq_to_adc(nint)),
        write_log(write_log) {

    // variables to load read words into
    uint32_t header = 0;
    std::vector<uint32_t> v_word;

    // reserve level 2 vectors
    size_t npix = static_cast<size_t>(nrow * ncol);
    v_cds_avg.reserve(npix);
    v_cds_var.reserve(npix);
    v_cts_avg.reserve(npix);
    v_cts_var.reserve(npix);
    
    // rever level 1 vectors
    size_t nskp = static_cast<size_t>(nrow * ncol * ndcm);
    v_cds.reserve(npix);
    v_cts.reserve(npix);

    // max size needed
    v_word.reserve(12);
    
    // Initialize buffer
    read_buffer_.resize(BUFFER_SIZE);
    buffer_pos_ = 0;
    buffer_valid_ = 0;

    // open data stream (picks PlainStream, ZstdStream, or BZ2Stream by extension)
    try {
        stream_ = make_stream(filename);
    } catch (const std::exception& e) {
        log(LogLevel::FATAL, "Unable to open " + filename + ": " + e.what());
        return;
    }
    log(LogLevel::INFO, "Decoding " + filename);
    
    // start reading file
    while(readNextWord(header)){
        
        // check which level the data packet is in
        bool is_lvl0 = (((header >> 30) & 0b11  ) == 0b00);
        bool is_lvl1 = (((header >> 28) & 0b1111) == 0b0101);
        bool is_lvl2 = (((header >> 28) & 0b1111) == 0b0111);
        bool is_lvl3 = !(is_lvl0 || is_lvl1 || is_lvl2);
        
        if(is_lvl3){
            decodeLevel3(header);
        }
        else if (is_lvl2){
            if (!readWordsAfterHeader(LVL2_SIZE-1, v_word, header)) break;
            if (v_word.size() < 11) v_word.resize(11, 0u);
            decodeLevel2(v_word);
        }
        else if (is_lvl1){
            if (!readWordsAfterHeader(LVL1_SIZE, v_word, header)) break;
            decodeLevel1(v_word);
        }
        else if (is_lvl0){
            decodeLevel0(header);
        }
    }

    check_errors();
    log(LogLevel::DEBUG, "Finished decoding " + filename);
}


BinaryDecoder BinaryDecoder::from_meta(std::string jsonfile, std::string binfile,
                                       uint32_t output_lvl, bool write_log)
{
    if (!std::filesystem::is_regular_file(jsonfile)) {
        Logger::getInstance().log(LogLevel::WARNING, "BinaryDecoder",
            "meta file not found, using defaults: " + jsonfile, write_log);
        return BinaryDecoder(std::move(binfile), output_lvl, write_log);
    }

    std::ifstream jf(jsonfile);
    nlohmann::json cfg = nlohmann::json::parse(jf);

    std::string routine = cfg.value<std::string>("ROUTINE", "");
    bool is_psdmode = (routine == "PSDMode");

    uint32_t ndcm = cfg.value<uint32_t>("NDCM", 1);
    uint32_t nrow = is_psdmode ? 0u : cfg.value<uint32_t>("NROW", 0);
    uint32_t ncol = is_psdmode ? 0u : cfg.value<uint32_t>("NCOL", 0);
    uint32_t nint = is_psdmode ? 0u : cfg.value<uint32_t>("delayInt", 0);

    return BinaryDecoder(std::move(binfile), output_lvl, write_log, nrow, ncol, ndcm, nint);
}


BinaryDecoder BinaryDecoder::from_bin(std::string binfile,
                                      uint32_t output_lvl, bool write_log)
{
    namespace fs = std::filesystem;

    fs::path bin_path(binfile);
    if (!fs::is_regular_file(bin_path))
        throw std::runtime_error("bin file does not exist: " + binfile);

    fs::path meta_path = bin_path;
    meta_path.replace_extension(".meta");

    return BinaryDecoder::from_meta(meta_path.string(), std::move(binfile),
                                    output_lvl, write_log);
}


// ----- Functions to read words in file ---------------------------------------//
bool BinaryDecoder::fillBuffer() {
    buffer_pos_ = 0;
    buffer_valid_ = 0;

    ssize_t bytes_read = stream_->read(read_buffer_.data(), BUFFER_SIZE);

    if (bytes_read <= 0) return false; // EOF or error

    buffer_valid_ = static_cast<size_t>(bytes_read);
    return true;
}


bool BinaryDecoder::readNextWord(uint32_t& word) {
    // Need 4 bytes for a word
    constexpr size_t WORD_SIZE = sizeof(uint32_t);
    
    // Check if we need to refill buffer
    if (buffer_pos_ + WORD_SIZE > buffer_valid_) {
        // Handle partial word at end of buffer
        if (buffer_pos_ < buffer_valid_) {
            // Copy remaining bytes to start of buffer
            size_t remaining = buffer_valid_ - buffer_pos_;
            std::memmove(read_buffer_.data(), 
                        read_buffer_.data() + buffer_pos_, 
                        remaining);
            
            // Read more data
            ssize_t bytes_read = stream_->read(
                read_buffer_.data() + remaining,
                BUFFER_SIZE - remaining);

            if (bytes_read <= 0) return false; // EOF
            
            buffer_pos_ = 0;
            buffer_valid_ = remaining + static_cast<size_t>(bytes_read);
        } else {
            // Buffer is empty, fill it
            if (!fillBuffer()) return false; // EOF
        }
        
        // Still not enough data after refill
        if (buffer_pos_ + WORD_SIZE > buffer_valid_) return false;
    }
    
    // Read word from buffer
    uint32_t tmp;
    std::memcpy(&tmp, read_buffer_.data() + buffer_pos_, WORD_SIZE);
    buffer_pos_ += WORD_SIZE;
    
    word = helper::be32_to_host(tmp);
    return true;
}



bool BinaryDecoder::readWordsAfterHeader(uint8_t n_total,
                                         std::vector<uint32_t>& out,
                                         uint32_t header)
{
    out.clear();
    out.reserve(n_total);

    if (n_total == 0) return true;

    out.push_back(header);

    for (std::size_t i = 1; i < n_total; ++i) {
        uint32_t w;
        if (!readNextWord(w)) return false;
        out.push_back(w);
    }
    return true;
}





// ----- Functions to decode Binary data coming from data stream  --------------//

void BinaryDecoder::decodeLevel0(uint32_t header) {
    // decode word and create data structure
    uint8_t clk = (header & (1u << 29)) ? 1u : 0u;
    uint8_t V2  = (header & (1u << 27)) ? 1u : 0u;
    uint8_t H2  = (header & (1u << 26)) ? 1u : 0u;
    uint8_t TG  = (header & (1u << 25)) ? 1u : 0u;
    uint8_t OG  = (header & (1u << 24)) ? 1u : 0u;
    uint8_t SW  = (header & (1u << 23)) ? 1u : 0u;
    uint8_t DG  = (header & (1u << 22)) ? 1u : 0u;
    uint8_t RU  = (header & (1u << 21)) ? 1u : 0u;
    uint8_t RD  = (header & (1u << 20)) ? 1u : 0u;

    // 20 LSB and twos complement to get from uint to int
    int32_t val = (int32_t)((header & 0xFFFFFu) << 12) >> 12;
    
    // calculate Level 1 outputs from Level 0 data
    if((RU==1u) || (RD==1u)){
        lvl0_accumulator.add(val);
    }

    // Fill data frame from start to end
    if ( (output_lvl == 0) && (cstart > 0) && (cend == 0) ){
        frame0.emplace_back(Level0Data{clk, V2, H2, TG, OG, SW, DG, RU, RD, val});
    }
}


void BinaryDecoder::decodeLevel1(std::vector<uint32_t>& v_word)
{
    // header [ramp, cyclic index, number of samples]
    uint32_t header = v_word[0];
    uint8_t ramp = (header & (1u << 27)) ? 1u : 0u;
    uint8_t idx  = static_cast<uint8_t>((header >> 20) & 0x7Fu);
    uint32_t n   = header & 0xFFFFFu;

    // sum of ADC samples
    int32_t sum  = helper::to_int32(v_word[1], RESERVE_BIT);

    // index pedestal/signal counter.
    ++idx_skp[ramp];
    
    // useful booleans for accumulators
    const bool is_pedestal = (ramp == 0);
    const bool is_first_pedestal = (is_pedestal && idx_skp[ramp] == 1);
    
    check_cyclic_idx(idx, ramp);
    check_nadc(n, ramp);
    check_accumulator1(n, sum, idx_skp[ramp], is_pedestal);
    lvl0_accumulator.reset();

    // calculate Level 2 outputs from Level 1 data
    lvl1_accumulator.add(n, sum, is_pedestal, is_first_pedestal);

    // computation
    if (is_pedestal and !is_first_pedestal){
        double cts = lvl1_accumulator.compute_cts();
        v_cts.push_back(cts);
    }
    else if (!is_pedestal){
        double cds = lvl1_accumulator.compute_cds();
        v_cds.push_back(cds);
    }

 
    // Fill data frame
    if(output_lvl <= 1){
        frame1.emplace_back(Level1Data{cepix, cline, idx_skp[ramp], ramp, idx, n, sum});
    }
}


void BinaryDecoder::decodeLevel2(std::vector<uint32_t>& v_word)
{
    // header [clock internal, cyclic index, number of adc samples]
    uint32_t header = v_word[0];
    uint8_t cin = (header & (1u << 27)) ? 1u : 0u;
    uint8_t idx = static_cast<uint8_t>((header >> 20) & 0x7Fu);
    uint32_t cds_n  = header & 0xFFFFF;

    // sum of samples
    int64_t cds_sum  = helper::to_int64(v_word[1], v_word[2], RESERVE_BIT);
    int64_t cds_sum2 = helper::to_int64(v_word[3], v_word[4], RESERVE_BIT);
    
    // number of samples in interpolation
    uint32_t header2 = v_word[5];
    uint32_t cts_n  = header2 & 0xFFFFF;

    // NOTE: for itp method (sum, sum2) area actually (2*itp_sum, 4*itp_sum2)
    int64_t cts_sum  = helper::to_int64(v_word[6], v_word[7], RESERVE_BIT);
    int64_t cts_sum2 = helper::to_int64(v_word[8], v_word[9], RESERVE_BIT);
    
    // int32_t ped_0 = helper::to_int32(v_word[10], RESERVE_BIT);
    // int32_t ped_n = helper::to_int32(v_word[11], RESERVE_BIT);


    // check clock, cyclic index, and number of adc samples in cds/cts
    check_clk(cin);
    check_cyclic_idx(idx, 0);
    check_n_cds(cds_n);
    check_n_cts(cts_n);
    check_accumulator2(cds_n, cds_sum, cds_sum2, cts_n, cts_sum, cts_sum2);

    // calculate average and variance
    double cds_avg(0), cds_var(0), cts_avg(0), cts_var(0);
    if (cds_n!=0) {
        const double d_cds_n     = static_cast<double>(cds_n);
        const double d_cds_sum   = static_cast<double>(cds_sum);
        const double d_cds_sum2  = static_cast<double>(cds_sum2);

        cds_avg  = d_cds_sum / d_cds_n ;
        cds_var  = ndcm  * d_cds_sum2 / (d_cds_n * d_cds_n) - cds_avg * cds_avg;
    }

    if ( (cts_n!=0) ) {
        const double d_cts_n     = static_cast<double>(cts_n);
        const double d_cts_sum   = static_cast<double>(cts_sum);
        const double d_cts_sum2  = static_cast<double>(cts_sum2);
        
        // NOTE: gain of 2 in itp_avg from digital logic
        cts_avg = 0.50 * d_cts_sum / d_cts_n;
        cts_var = 0.25 * ndcm * d_cts_sum2 / (d_cts_n * d_cts_n) - cts_avg * cts_avg;
    }
    v_cds_avg.push_back(cds_avg);
    v_cds_var.push_back(cds_var);
    v_cts_avg.push_back(cts_avg);
    v_cts_var.push_back(cts_var);

    // Fill data frame
    if(output_lvl <= 2){
        frame2.emplace_back(
            Level2Data{
                cepix, cline, cin, idx, cds_n, cds_sum, cds_sum2, cts_n, cts_sum, cts_sum2
            }
        );
    }

    // reset counter for number of skips
    idx_skp[0] = 0;
    idx_skp[1] = 0;
    lvl1_accumulator.reset();
}


void BinaryDecoder::decodeLevel3(uint32_t word) {
    // 3 Letter ASCII commands from ACM
    static constexpr uint32_t ASCII_START = 0xd3544152; // 11010011010101000100000101010010
    static constexpr uint32_t ASCII_END   = 0xa0454e44; // 10100000010001010100111001000100
    static constexpr uint32_t ASCII_BUSY  = 0xc2555359; // 11000010010101010101001101011001
    static constexpr uint32_t ASCII_PONG  = 0xd04f4e47; // 11010000010011110100111001000111
    static constexpr uint32_t ASCII_LINE  = 0x4c494e45; // 01001100010010010100111001000101
    static constexpr uint32_t ASCII_EPIX  = 0x45504958; // 01000101010100000100100101011000
    static constexpr uint32_t ASCII_LVL0  = 0x4c564c30; // 01001100010101100100110000110000
    static constexpr uint32_t ASCII_LVL1  = 0x4c564c31; // 01001100010101100100110000110001
    static constexpr uint32_t ASCII_CLKI  = 0xc34c4b49; // 11000011010011000100101101001001
    static constexpr uint32_t ASCII_CLKE  = 0xc34c4b45; // 11000011010011000100101101000101
    
    // EPIX word, no minimum bias override
    static constexpr int EPIX_NO_MB = 3;


    switch (word) {
        case ASCII_EPIX:  handleEndOfPixel(EPIX_NO_MB);  break;
        case ASCII_LINE:  cline++;                       break;
        case ASCII_CLKI:  cclki++;                       break;
        case ASCII_CLKE:  cclke++;                       break;
        case ASCII_PONG:  cpong++;                       break;
        case ASCII_START: cstart++;                      break;
        case ASCII_BUSY:  cbusy++;                       break;
        case ASCII_END:   cend++;                        break;
        case ASCII_LVL0:  handleEndOfPixel(0);           break;
        case ASCII_LVL1:  handleEndOfPixel(1);           break;
        default: {
            const bool is_info_word = cstart > 0 && cbusy > 0 && info_word == 0;
            if (is_info_word) {
                decodeInfoWord(word);
            } else {
                log(LogLevel::WARNING, "Unknown word: 0x" + helper::uint_to_hex(word));
            }
            break;
        }
    }
}


void BinaryDecoder::decodeInfoWord(uint32_t word){
    info_word    =  word;
    RESERVE_BIT  = (word >> 30) & MASKU32(1);
    ITP_FIRMWARE = (word >> 29) & MASKU32(1);
    LVL1_SIZE    = (word >> 22) & MASKU32(4);
    LVL2_SIZE    = (word >> 18) & MASKU32(4);
    LVL          = (word >> 16) & MASKU32(2);
    LV_FREQ      =  word        & MASKU32(16);

    // initialize pixel level to image-wide default
    pix_lvl = LVL;

    // backwards compatibility (Dec 2025 and before)
    if (LVL1_SIZE == 0) LVL1_SIZE = 2;
    if (LVL2_SIZE == 0) LVL2_SIZE = 6;
}


void BinaryDecoder::handleEndOfPixel(int mb_lvl){
    cepix++;

    
    if (mb_lvl == 0 || mb_lvl == 1) {
        pix_lvl = mb_lvl;   // minimum bias data for pixel
        log(LogLevel::DEBUG, "Switching to Level " + std::to_string(mb_lvl) + 
                             " for pixel " + std::to_string(cepix));
    } else { 
        pix_lvl = LVL; // use image-wide level from info word
    }
}





// -----------------------------------------------

void BinaryDecoder::check_nadc(uint32_t n, uint8_t ramp) {
    if (nadc == 0 || n == nadc)
        return;

    log(LogLevel::DEBUG, "n_adc expected " + std::to_string(nadc) +
                         " received "      + std::to_string(n) +
                         " at pixel "      + std::to_string(cepix) +
                         ", skip "         + std::to_string(idx_skp[ramp]));
    error_nadc = true; 
}

void BinaryDecoder::check_n_cds(uint32_t n_cds) {
    
    if (nadc == 0) return;


    uint32_t n_cds_adc = nadc * ndcm;

    if (n_cds == n_cds_adc ||
        (n_cds_adc >= nadc && n_cds == n_cds_adc - nadc))
        return;

    log(LogLevel::DEBUG, "N_CDS expected " + std::to_string(n_cds_adc) +
                         " received "      + std::to_string(n_cds) +
                         " at pixel "      + std::to_string(cepix));
    error_nadc = true; 
}

void BinaryDecoder::check_n_cts(uint32_t n_cts) {
    
    if (nadc == 0) return;

    uint32_t n_cts_adc = nadc * (ndcm + 1);

    if (n_cts == n_cts_adc ||
        (n_cts_adc >= nadc && n_cts == n_cts_adc - nadc))
        return;

    log(LogLevel::DEBUG, "N_CTS expected " + std::to_string(n_cts_adc) +
                         " received "      + std::to_string(n_cts) +
                         " at pixel "      + std::to_string(cepix));
    error_nadc = true; 
}


void BinaryDecoder::check_cyclic_idx(uint8_t idx, uint8_t ramp) {
    const uint8_t expected = static_cast<uint8_t>((idx_prev + 1) & 0x7F);
    if (idx == expected) { idx_prev = idx; return; }

    std::string msg = "Cyclic index inconsistency at pixel " + std::to_string(cepix)
                    + ", skip "      + std::to_string(idx_skp[ramp])
                    + " | prev="     + std::to_string(static_cast<int>(idx_prev))
                    + " expected="   + std::to_string(static_cast<int>(expected))
                    + " current="    + std::to_string(static_cast<int>(idx));
    log(LogLevel::DEBUG, msg);
    error_idx = true;
    idx_prev = idx;
}



void BinaryDecoder::check_clk(uint8_t cin) {
    if (cin_prev == cin) { cin_prev = cin; return; }
    if (cin_prev != static_cast<uint8_t>(0xFF)) { // skip if uninitialized
        std::string msg = "Clock switched from "
                        + std::string(cin_prev == 1 ? "internal" : "external")
                        + " to "
                        + std::string(cin == 1 ? "internal" : "external")
                        + " at pixel " + std::to_string(cepix);
        log(LogLevel::DEBUG, msg);
        error_cin = true;
    }
    cin_prev = cin;
}


void BinaryDecoder::check_accumulator1(uint32_t fpga_n, int32_t fpga_sum,
                                        uint32_t skp_idx, bool is_pedestal){
    // There is no data to check against
    if (pix_lvl >= 1) return;

    const auto& acc = lvl0_accumulator;
    const bool fail_n   = (acc.n   != fpga_n);
    const bool fail_sum = (acc.sum != fpga_sum);
    if (!fail_n && !fail_sum) return;

    error_acc = true;
    const std::string ramp_type = is_pedestal ? "ped" : "sig";
    std::string msg = "Level 0->1 mismatch at pixel " + std::to_string(cepix)
                    + ", " + ramp_type + " " + std::to_string(skp_idx);
    if (fail_n)   msg += " | n: FPGA="   + std::to_string(fpga_n)   + " C++=" + std::to_string(acc.n);
    if (fail_sum) msg += " | sum: FPGA=" + std::to_string(fpga_sum) + " C++=" + std::to_string(acc.sum);
    log(LogLevel::DEBUG, msg);
}


void BinaryDecoder::check_accumulator2(uint32_t fpga_cds_n, int64_t fpga_cds_sum, int64_t fpga_cds_sum2,
                                        uint32_t fpga_cts_n, int64_t fpga_cts_sum, int64_t fpga_cts_sum2){

    // There is no data to check against
    if (pix_lvl >= 2) return;

    const auto& acc = lvl1_accumulator;
    const bool fail_cds_n    = (acc.cds_n    != fpga_cds_n);
    const bool fail_cds_sum  = (acc.cds_sum  != fpga_cds_sum);
    const bool fail_cds_sum2 = (acc.cds_sum2 != fpga_cds_sum2);
    const bool fail_cts_n    = ITP_FIRMWARE && (acc.cts_n    != fpga_cts_n);
    const bool fail_cts_sum  = ITP_FIRMWARE && (acc.cts_sum  != fpga_cts_sum);
    const bool fail_cts_sum2 = ITP_FIRMWARE && (acc.cts_sum2 != fpga_cts_sum2);

    if (!fail_cds_n && !fail_cds_sum && !fail_cds_sum2 &&
        !fail_cts_n && !fail_cts_sum && !fail_cts_sum2) return;

    error_acc = true;
    std::string msg = "Level 1->2 mismatch at pixel " + std::to_string(cepix);
    if (fail_cds_n)    msg += " | cds_n: FPGA="    + std::to_string(fpga_cds_n)    + " C++=" + std::to_string(acc.cds_n);
    if (fail_cds_sum)  msg += " | cds_sum: FPGA="  + std::to_string(fpga_cds_sum)  + " C++=" + std::to_string(acc.cds_sum);
    if (fail_cds_sum2) msg += " | cds_sum2: FPGA=" + std::to_string(fpga_cds_sum2) + " C++=" + std::to_string(acc.cds_sum2);
    if (fail_cts_n)    msg += " | cts_n: FPGA="    + std::to_string(fpga_cts_n)    + " C++=" + std::to_string(acc.cts_n);
    if (fail_cts_sum)  msg += " | cts_sum: FPGA="  + std::to_string(fpga_cts_sum)  + " C++=" + std::to_string(acc.cts_sum);
    if (fail_cts_sum2) msg += " | cts_sum2: FPGA=" + std::to_string(fpga_cts_sum2) + " C++=" + std::to_string(acc.cts_sum2);
    log(LogLevel::DEBUG, msg);
}


void BinaryDecoder::check_errors(){

    auto log_helper = [this](bool is_error, const std::string& msg){
        if(is_error){
            log(LogLevel::WARNING, msg + " invalid");
        }
        else{
            log(LogLevel::DEBUG, msg + " valid");
        }
    };

    log_helper(error_acc,  "accumulation of sum");
    log_helper(error_idx,  "cyclic index");
    log_helper(error_cin,  "clock stability");
    log_helper(error_nadc, "number of samples");
}













