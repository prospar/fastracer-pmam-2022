; RUN: llc < %s  -mcpu=core-avx2 > /dev/null
; This test crashed on variable shift creation on AVX2

target datalayout = "e-m:e-p:32:32-f64:32:64-f80:32-n8:16:32-S128"
target triple = "i386-unknown-linux-gnu"

define void @f_f(float* noalias nocapture %RET, float %aFOO, i32 %div)  {
allocas:
  %__idiv_table_u32_offset10_offset_load.i = add i64 0, -2
  br label %if_then18.i

if_then18.i:
  %aFOO_load_to_uint32 = fptoui float %aFOO to i32
  %aFOO_load_to_uint32_broadcast_init = insertelement <8 x i32> undef, i32 %aFOO_load_to_uint32, i32 0
  %aFOO_load_to_uint32_broadcast = shufflevector <8 x i32> %aFOO_load_to_uint32_broadcast_init, <8 x i32> undef, <8 x i32> zeroinitializer

  %multiplier_load_broadcast_init.i = insertelement <8 x i64> undef, i64 2, i32 0
  %multiplier_load_broadcast.i = shufflevector <8 x i64> %multiplier_load_broadcast_init.i, <8 x i64> undef, <8 x i32> zeroinitializer
  %numerator_load_to_uint64.i = zext <8 x i32> %aFOO_load_to_uint32_broadcast to <8 x i64>

  ;if replace '%__idiv_table_u32_offset10_offset_load.i' with '-2' or remove 'if_then18.i' label the error disappears
  %add__shift_load21.i = add i64 %__idiv_table_u32_offset10_offset_load.i, 32
  %add__shift_load21_broadcast_init.i = insertelement <8 x i64> undef, i64 %add__shift_load21.i, i32 0
  %add__shift_load21_broadcast.i = shufflevector <8 x i64> %add__shift_load21_broadcast_init.i, <8 x i64> undef, <8 x i32> zeroinitializer

  %mul_val_load_mult_load.i = mul <8 x i64> %numerator_load_to_uint64.i, %multiplier_load_broadcast.i
  %bitop22.i = lshr <8 x i64> %mul_val_load_mult_load.i, %add__shift_load21_broadcast.i
  %bitop22_to_uint32.i = trunc <8 x i64> %bitop22.i to <8 x i32>
  br label %__fast_idiv___UM_vyuunu.exit


__fast_idiv___UM_vyuunu.exit:                   
  %calltmp_to_float = uitofp <8 x i32> %bitop22_to_uint32.i to <8 x float>
  %ptrcast = bitcast float* %RET to <8 x float>*
  store <8 x float> %calltmp_to_float, <8 x float>* %ptrcast, align 4
  ret void
}

