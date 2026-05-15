#pragma once

#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdint>
#include <string>
#include <sstream>
#include <vector>
#include <memory>

#include "stream.hpp"
#include "decoder_data.hpp"
#include "logger.hpp"



// A simple class with a constructor and some methods...
class BinaryDecoder {
public:
    //Constructor
    BinaryDecoder(std::string fname,
                  uint32_t output_lvl=0,
                  bool write_log=false,
                  LogLevel print_level=LogLevel::INFO,
                  uint32_t nrow=0,
                  uint32_t ncol=0,
                  uint32_t ndcm=1,
                  uint32_t nint=0);

    // Factory: parse JSON metadata, then construct with correct parameters
    static BinaryDecoder from_meta(std::string jsonfile, std::string binfile,
                                   uint32_t output_lvl=0, bool write_log=false,
                                   LogLevel print_level=LogLevel::INFO);

    // Factory: look for a sibling <stem>.meta file next to binfile.
    // If present, delegate to from_meta; otherwise construct with defaults.
    static BinaryDecoder from_bin(std::string binfile,
                                  uint32_t output_lvl=0, bool write_log=false,
                                  LogLevel print_level=LogLevel::INFO);

    //Destructor
    ~BinaryDecoder() {
        if (stream_) stream_->close();
    }

    // Enable move (suppressed by user-defined destructor)
    BinaryDecoder(BinaryDecoder&&) = default;
    BinaryDecoder& operator=(BinaryDecoder&&) = default;
    
    // basic file information
    std::string filename; // file that was decoded
    uint32_t output_lvl;  // what level data frames to keep
    uint32_t nrow;        // number of rows in the image
    uint32_t ncol;        // number of columns in the image
    uint32_t ndcm;        // non-destructive charge measurements
    uint32_t nadc;        // number of samples in a single integration window
    
    // info word
    uint32_t info_word{0u};    // third word after START and BUSY
    uint32_t RESERVE_BIT{0u};  // bit 31 of every word is reserved as 0
    uint32_t ITP_FIRMWARE{0u}; // interpolation done on firmware
    uint32_t LVL{5u};          // level data was taken at
    uint32_t LVL1_SIZE{0u};    // number of words in LVL1 Data
    uint32_t LVL2_SIZE{0u};    // number of words in LVL2 Data
    uint32_t LV_FREQ{0u};      // frequency of level data
    
    // counters
    uint32_t cclki{0};  //< counter number of times switched to internal clock
    uint32_t cclke{0};  //< counter number of times switched to external clock
    uint32_t cepix{0};  //< pixel counter (number of Dump Gates)
    uint32_t cline{0};  //< line counter (number of Transfer Gates)
    uint32_t cpong{0};  //< pong counter (pongs from ACM)
    uint32_t cstart{0}; //< received start
    uint32_t cbusy{0};  //< received busy
    uint32_t cend{0};   //< received end
    
    // Book keeping of errors
    bool error_acc{false};  // error in accumulation of any level
    bool error_idx{false};  // error in cyclic index
    bool error_cin{false};  // error in clock internal/external switch
    bool error_nadc{false}; // error in # of adc samples in any level
    
    // data from 3 levels
    std::vector<Level0Data> frame0;     // stores level0 data when at frame_lvl 0
    std::vector<Level1Data> frame1;     // stores level1 data when at frame_lvl 1
    std::vector<Level2Data> frame2;     // stores level2 data when at frame_lvl 2
    
    // level 2 images
    std::vector<double> v_cds_avg{};  ///< average values calc from L2
    std::vector<double> v_cds_var{};  ///< variance values calc from L2
    std::vector<double> v_cts_avg{};  ///< interpolated average from L2
    std::vector<double> v_cts_var{};  ///< interpolated variance from L2
    
    // level 1 image
    std::vector<double> v_cds{};   ///< single correlated double sampling measurements
    std::vector<double> v_cts{};   ///< single correlated tripple sampling measurements


private:
    bool write_log{false};     // write to log file if true
    uint32_t pix_lvl{3u};      // data level of current pixel
    std::vector<uint16_t> idx_skp{0, 0}; // keep track of RU RD index for given pixel
    
    // accumulate sums from level 0 and level 1 to compute level 1 and level 2
    Level0Accumulator lvl0_accumulator;
    Level1Accumulator lvl1_accumulator;
    
    static constexpr size_t BUFFER_SIZE = 1024 * 1024; // 1MB buffer
    std::vector<uint8_t> read_buffer_;
    size_t buffer_pos_{0};        // Position in buffer
    size_t buffer_valid_{0};      //
    std::unique_ptr<InputStream> stream_;  // Compressed or plain input

    bool fillBuffer();
    bool readNextWord(uint32_t& word);
    bool readWordsAfterHeader(uint8_t n_total, 
                              std::vector<uint32_t>& out,
                              uint32_t header);


    // check if values change from pixel to pixel and skip to skip
    uint8_t  cin_prev{0xFFu};     // previous pixel clock
    uint8_t  idx_prev{0xFFu};     // cyclical index counter in c++


    // check
    void check_nadc(uint32_t n, uint8_t ramp);
    void check_n_cds(uint32_t n);
    void check_n_cts(uint32_t n);
    void check_cyclic_idx(uint8_t idx, uint8_t ramp);
    void check_clk(const uint8_t cin);
    void check_accumulator1(uint32_t fpga_n, int32_t fpga_sum,
                            uint32_t skp_idx, bool is_pedestal);
    void check_accumulator2(uint32_t fpga_cds_n, int64_t fpga_cds_sum, int64_t fpga_cds_sum2,
                            uint32_t fpga_cts_n, int64_t fpga_cts_sum, int64_t fpga_cts_sum2);
    void check_errors();

    // functions to decode
    void decode();
    void decodeLevel0(uint32_t header);  //< decode level0 data in the stream
    void decodeLevel1(std::vector<uint32_t>& v_word);  //< decode level1 data in the stream
    void decodeLevel2(std::vector<uint32_t>& v_word);  //< decode level2 data in the stream
    void decodeLevel3(uint32_t word);   //< decode ACII data
    
    // helper functions for decodeLevel3
    void handleEndOfPixel(int mb_lvl);
    void decodeInfoWord(uint32_t word); //< decode info word (after START and BUSY)
       
    void log(LogLevel log_level, const std::string& msg) {
        Logger::getInstance().log(log_level, "BinaryDecoder", msg, write_log);
    }

};

