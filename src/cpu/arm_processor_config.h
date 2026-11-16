#pragma once

#include <cstdint>

#include <cerfcore/core/service.h>

namespace cerfcore {

/* ARM DDI 0406C.c B3.5: whether a VMSAv7 implementation supports Supersections
   is IMPLEMENTATION DEFINED, and the extended base address in descriptor
   bits[8:5, 23:20] is a further option on top. */
enum class ArmSupersectionFormat : uint8_t {
    kUnknown,
    kNone,
    kPa32,
    kPa36,
};

class ArmProcessorConfig : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t PcStoreOffset()          const { return pc_store_offset_; }
    bool     BaseRestoredAbortModel() const { return base_restored_abort_model_; }
    uint32_t Midr()                   const { return midr_; }
    uint32_t Ctr()                    const { return ctr_; }

    bool HasDsp()             const { return dsp_; }
    bool HasLoadStoreDouble() const { return load_store_double_; }

    /* DDI 0406C.c A1.3, p. A1-30: ARMv5TE "Adds Preload Data (PLD),
       Load Register Dual (LDRD), Store Register Dual (STRD)". */
    bool HasPreload() const { return preload_; }

    bool HasThumb() const { return thumb_; }

    bool HasThumb2() const { return thumb2_; }

    /* DDI0406C §A2.3.1: LDR/POP/LDM with Rt==PC interwork (bit 0
       selects the ISA state) from ARMv5T on; on v4T they branch
       remaining in the current ISA state. */
    bool HasLoadToPcInterworking() const { return load_to_pc_interworking_; }

    /* DDI0406C §A2.3.1: ARM-state data-processing with Rd==PC and
       no flag-setting interworks only from ARMv7 on; earlier
       versions branch remaining in the current ISA state. */
    bool HasDataProcToPcInterworking() const { return data_proc_to_pc_interworking_; }

    bool HasClz()    const { return clz_; }
    bool HasBlxReg() const { return blx_reg_; }

    bool HasArmv5UnconditionalSpace() const { return armv5_unconditional_space_; }

    /* ARM DDI 0100I p. A4-75: "Accessing the SPSR when in User mode or
       System mode is UNPREDICTABLE." */
    bool AccessesSpsrInSystemMode() const { return accesses_spsr_in_system_mode_; }

    bool HasMls()           const { return mls_; }
    bool HasMovwMovt()      const { return movw_movt_; }
    bool HasBitField()      const { return bit_field_; }
    bool HasRev()           const { return rev_; }
    bool HasExtendRotate()  const { return extend_rotate_; }
    bool HasLdrexStrex()    const { return ldrex_strex_; }
    bool HasLdrexStrexV6k() const { return ldrex_strex_v6k_; }
    bool HasBarrierInsn()   const { return barrier_insn_; }

    /* DDI 0406C.c A4.4.8, p. A4-172: SDIV and UDIV are OPTIONAL in an ARMv7-A
       implementation without the Virtualization Extensions, and
       ID_ISAR0.Divide_instrs indicates the level of support. */
    bool HasIntegerDivide() const { return integer_divide_; }

    /* ARM DDI 0344K Table 3-3, p. 3-17: Cortex-A8 allocates the L1 system
       array debug operations at c15, write-only and Secure state only. */
    bool HasL1SystemArrayDebug() const { return l1_system_array_debug_; }

    bool HasCp15V6()             const { return cp15_v6_; }
    bool HasCp15V7()             const { return cp15_v7_; }
    bool HasSecurityExtensions() const { return security_extensions_; }

    bool HasMultiprocessingExtensions() const { return multiprocessing_extensions_; }

    uint32_t IdPfr0()     const { return id_pfr0_; }
    uint32_t IdPfr1()     const { return id_pfr1_; }
    uint32_t IdDfr0()     const { return id_dfr0_; }
    uint32_t IdAfr0()     const { return id_afr0_; }
    uint32_t IdMmfr0()    const { return id_mmfr0_; }
    uint32_t IdMmfr1()    const { return id_mmfr1_; }
    uint32_t IdMmfr2()    const { return id_mmfr2_; }
    uint32_t IdMmfr3()    const { return id_mmfr3_; }
    uint32_t IdIsar0()    const { return id_isar0_; }
    uint32_t IdIsar1()    const { return id_isar1_; }
    uint32_t IdIsar2()    const { return id_isar2_; }
    uint32_t IdIsar3()    const { return id_isar3_; }
    uint32_t IdIsar4()    const { return id_isar4_; }
    uint32_t IdIsar5()    const { return id_isar5_; }
    uint32_t Tlbtr()      const { return tlbtr_; }
    uint32_t Mpidr()      const { return mpidr_; }
    uint32_t Tcmtr()      const { return tcmtr_; }
    uint32_t Periphbase() const { return periphbase_; }

    ArmSupersectionFormat SupersectionFormat() const { return supersection_format_; }

    bool HasL2CacheAuxControl()  const { return l2_cache_aux_control_; }
    bool HasAuxControlRegister() const { return aux_control_register_; }

    uint32_t Clidr() const { return clidr_; }
    uint32_t Ccsidr(uint32_t csselr) const;

    bool     HasVfp()  const { return vfp_; }
    bool     HasNeon() const { return neon_; }
    uint32_t Fpsid()   const { return fpsid_; }
    uint32_t Mvfr0()   const { return mvfr0_; }
    uint32_t Mvfr1()   const { return mvfr1_; }

private:
    uint32_t pc_store_offset_ = 0;
    uint32_t midr_            = 0;
    uint32_t ctr_             = 0;
    uint32_t clidr_           = 0;
    uint32_t ccsidr_l1i_      = 0;
    uint32_t ccsidr_l1d_      = 0;
    uint32_t ccsidr_l2_       = 0;
    uint32_t fpsid_           = 0;
    uint32_t mvfr0_           = 0;
    uint32_t mvfr1_           = 0;

    ArmSupersectionFormat supersection_format_ = ArmSupersectionFormat::kNone;

    bool base_restored_abort_model_    = false;
    bool thumb_                        = false;
    bool thumb2_                       = false;
    bool dsp_                          = false;
    bool load_store_double_            = false;
    bool preload_                      = false;
    bool clz_                          = false;
    bool blx_reg_                      = false;
    bool armv5_unconditional_space_    = false;
    bool load_to_pc_interworking_      = false;
    bool data_proc_to_pc_interworking_ = false;
    bool accesses_spsr_in_system_mode_ = false;
    bool mls_                          = false;
    bool movw_movt_                    = false;
    bool bit_field_                    = false;
    bool rev_                          = false;
    bool extend_rotate_                = false;
    bool ldrex_strex_                  = false;
    bool ldrex_strex_v6k_              = false;
    bool barrier_insn_                 = false;
    bool integer_divide_               = false;
    bool cp15_v6_                      = false;
    bool cp15_v7_                      = false;
    bool security_extensions_          = false;
    bool multiprocessing_extensions_   = false;
    uint32_t id_pfr0_                  = 0;
    uint32_t id_pfr1_                  = 0;
    uint32_t id_dfr0_                  = 0;
    uint32_t id_afr0_                  = 0;
    uint32_t id_mmfr0_                 = 0;
    uint32_t id_mmfr1_                 = 0;
    uint32_t id_mmfr2_                 = 0;
    uint32_t id_mmfr3_                 = 0;
    uint32_t id_isar0_                 = 0;
    uint32_t id_isar1_                 = 0;
    uint32_t id_isar2_                 = 0;
    uint32_t id_isar3_                 = 0;
    uint32_t id_isar4_                 = 0;
    uint32_t id_isar5_                 = 0;
    uint32_t tlbtr_                    = 0;
    uint32_t mpidr_                    = 0;
    uint32_t tcmtr_                    = 0;
    uint32_t periphbase_               = 0;
    bool aux_control_register_         = false;
    bool l2_cache_aux_control_         = false;
    bool l1_system_array_debug_        = false;
    bool vfp_                          = false;
    bool neon_                         = false;
};

}
