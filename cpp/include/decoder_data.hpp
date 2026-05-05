#pragma once

#include <cstdint>

#pragma pack(push, 1)
struct Level0Data{
    uint8_t clk = 0;   // internal or external
    uint8_t V2 = 0;    
    uint8_t H2 = 0;
    uint8_t TG = 0;
    uint8_t OG = 0;
    uint8_t SW = 0;
    uint8_t DG = 0;
    uint8_t RU = 0;   // Signal
    uint8_t RD = 0;   // Pedestal
    int32_t val = 0;
};
#pragma pack(pop)
static_assert(sizeof(Level0Data) == 13, "Level0Data must be 13 bytes");


#pragma pack(push, 1)
struct Level1Data{
    uint32_t pix = 0;       // pixel number
    uint32_t line = 0;      // line number
    uint16_t skip = 0;      // skip number
    uint8_t ramp = 0;       // RD/pedestal (0) or RU/signal (1)
    uint8_t idx = 0;        // cyclic index from 0 to 127
    uint32_t n = 0;         // number of samples in window 
    int32_t  sum = 0;       // Sum[p_i]
};
#pragma pack(pop)
static_assert(sizeof(Level1Data) == 20, "Level1Data must be 20 bytes");


#pragma pack(push, 1)
struct Level2Data{
    uint32_t pix = 0;       // the pixel number
    uint32_t line = 0;      // the line number
    uint8_t cin = 0;        // is the clock internal or not
    uint8_t idx = 0;        // cyclic index from 0 to 127
    uint32_t n = 0;         // number of samples in the signal
    int64_t sum  = 0;       // Sum[p_i]
    int64_t sum2 = 0;       // Sum[p_i^2]
    uint32_t itp_n = 0;     // number of samples in the pedestal (- # in one integration)
    int64_t itp_sum  = 0;   // Sum[p_i]
    int64_t itp_sum2 = 0;   // Sum[p_i^2]
    // int32_t ped_0 = 0;   // first pedestal
    // int32_t ped_n = 0;   // last pedestal
}; 
#pragma pack(pop)
static_assert(sizeof(Level2Data) == 50, "Level2Data must be 50 bytes");



// Check Level 1 data from FPGA by accumulating Level 0 data
struct Level0Accumulator {
    uint32_t n   = 0;   // number of adc samples
    int32_t  sum = 0;   // sum of adc samples

    void reset() { *this = Level0Accumulator{}; }

    void add(int32_t adc_val){
        sum += adc_val;
        ++n;
    }
};


// Check Level 2 data from FPGA by accumulating Level 1 data
struct Level1Accumulator {
    // pedestal state for CDS and CTS calculation
    int64_t prev_pedestal_sum = 0;
    int64_t pedestal_sum  = 0;
    int64_t signal_sum  = 0;
    int64_t prev_pedestal_n = 0;
    int64_t pedestal_n  = 0;
    int64_t signal_n  = 0;


    // correlated double sampling
    uint32_t cds_n    = 0;
    int64_t  cds_sum  = 0;
    int64_t  cds_sum2 = 0;

    // correlated triple sampling
    uint32_t cts_n    = 0;
    int64_t  cts_sum  = 0;
    int64_t  cts_sum2 = 0;
   

    void reset() { *this = Level1Accumulator{}; }


    void add(uint32_t n, int32_t sum, bool is_pedestal, bool is_first_pedestal){
        if (is_pedestal){
            prev_pedestal_sum = pedestal_sum;
            pedestal_sum = static_cast<int64_t>(sum);

            prev_pedestal_n = pedestal_n;
            pedestal_n = n;

            if (!is_first_pedestal){
                accumulate_cts();
            }
        } else {
            signal_sum = static_cast<int64_t>(sum);
            signal_n = n;
            accumulate_cds();
        }
    }

    void accumulate_cts(){
        int64_t cts_sum_i  = 2 * signal_sum - pedestal_sum - prev_pedestal_sum;
        int64_t cts_sum2_i = cts_sum_i * cts_sum_i;
        
        cts_n    += pedestal_n;
        cts_sum  += cts_sum_i;
        cts_sum2 += cts_sum2_i;
    }

    
    void accumulate_cds(){
        int64_t cds_sum_i  = signal_sum - pedestal_sum;
        int64_t cds_sum2_i = cds_sum_i * cds_sum_i;

        cds_n    += signal_n;
        cds_sum  += cds_sum_i;
        cds_sum2 += cds_sum2_i;
    }

    double compute_cds(){
        double ped_sum = static_cast<double>(pedestal_sum);
        double sig_sum = static_cast<double>(signal_sum);
        double ped_n   = static_cast<double>(pedestal_n);
        double sig_n   = static_cast<double>(signal_n);

        return sig_sum/sig_n - ped_sum/ped_n;
    }


    double compute_cts(){
        double p_ped_sum = static_cast<double>(prev_pedestal_sum);
        double   ped_sum = static_cast<double>(pedestal_sum);
        double   sig_sum = static_cast<double>(signal_sum);
        double p_ped_n   = static_cast<double>(prev_pedestal_n);
        double   ped_n   = static_cast<double>(pedestal_n);
        double   sig_n   = static_cast<double>(signal_n);

        return sig_sum/sig_n - 0.5 * ped_sum/ped_n - 0.5 * p_ped_sum/p_ped_n;
    }
};


