# registry.py
from __future__ import annotations
from typing import Dict

from rotor_and_reflector import Rotor, Reflector   # adjust import path if needed

# ── Core Alphabets (must match rest of system) ───────────────────────────
ALPHA26 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
ALPHA38 = ALPHA26 + "0123456789#/"
ALPHA68 = ALPHA38 + "!\"$%&'()*+,-.:;<=>?@[\\]^_`{|}~"

# Legacy rotors ----------------------------------------------------------
I   = Rotor("EKMFLGDQVZNTOWYHXUSPAIBRCJ", notches="Q",  alphabet=ALPHA26)
II  = Rotor("AJDKSIRUXBLHWTMCQGZNPYFVOE", notches="E",  alphabet=ALPHA26)
III = Rotor("BDFHJLCPRTXVZNYEIWGAKMUSQO", notches="V",  alphabet=ALPHA26)
IV  = Rotor("ESOVPZJAYQUIRHXLNFTGKDCMWB", notches="J",  alphabet=ALPHA26)
V   = Rotor("VZBRGITYUPSDNHLXAWMJQOFECK", notches="Z",  alphabet=ALPHA26)
VI  = Rotor("JPGVOUMFYQBENHZRDKASXLICTW", notches="ZM", alphabet=ALPHA26)
VII = Rotor("NZJHGRCXMYSWBOUFAIVLPEKQDT", notches="ZM", alphabet=ALPHA26)

# Legacy reflectors ------------------------------------------------------
A = Reflector("EJMZALYXVBWFCRQUONTSPIKHGD", alphabet=ALPHA26)
B = Reflector("YRUHQSLDPXNGOKMIEBFZCWVJAT", alphabet=ALPHA26)
C = Reflector("FVPJIAOYEDRZXWGCTKUQSBNMHL", alphabet=ALPHA26)

#INOPv1
R1 = Rotor('BXML2UOKH3#46705CYG19ETFPRID8SWQAVNZJ/', "", alphabet=ALPHA38)
R2 = Rotor('1Q27#CPZL3RHV6MKTJUXFBE5O9N0AS4DI/YWG8', "", alphabet=ALPHA38)
R3 = Rotor('ZO5RNUBY/K0SMTPAJWCX23EDL8FG9VH4176I#Q', "", alphabet=ALPHA38)
R4 = Rotor('6V10Z/8FHED9S73AMRT#KQGJCPLUOY5XIN2BW4', "", alphabet=ALPHA38)
R5 = Rotor('MUIC7Y09E/WZ4OHS3T6Q82PV#B5XNFD1AJRLGK', "", alphabet=ALPHA38)
R6 = Rotor('U85N9QOGZ6BC4XLS70Y/VWIHF#1MPADK23EJRT', "", alphabet=ALPHA38)
R7 = Rotor('5IASENWMJDQK1H38O2#P6T4RZBYX0U9VLCGF7/', "", alphabet=ALPHA38)
R8 = Rotor('Q6OLVEN1J8FWH3/7CY#KXAG4I2STBUZD05MP9R', "", alphabet=ALPHA38)
R9 = Rotor('TF/VKHQM1LRPX74WDC0BEAS89NY52IJU6GZ#O3', "", alphabet=ALPHA38)
R10 = Rotor('BOQEW5NCLHZTR748S2F90UP6#MYVGX/DK1I3AJ', "", alphabet=ALPHA38)

D = Reflector('QZN6I4W9EY2V7CUTA8/POLG#JB10K5F3DMRHXS', alphabet=ALPHA38)
E = Reflector('RCBYWIPTFU#97X/G1A3HJ5END26QZS8V0M4LKO', alphabet=ALPHA38)
F = Reflector('V62P9W1#4S3MLYQDO8J7XAFUN0ZGCKI/BTREH5', alphabet=ALPHA38)
G = Reflector('#YRLN3ZPUXTD9E4H6C5KI07JBGV21FOSQW/MA8', alphabet=ALPHA38)
H = Reflector('SG#L0/B8X4PDT3WK51AMVUOI62ERZNJQY9H7CF', alphabet=ALPHA38)

# INOPv2
S1 = Rotor('_>`RA*}~M1^\'04.(Q%)-|CS]52V7JL:XDW!TB=H{N$&+</6K3ZEU@["\\#GYO?9F,;P8I', "", alphabet=ALPHA68)
S2 = Rotor(']YK#F~C{A")TR0SUHNV<?%B/(5-$`=9Z*6L>O:812+;,|EDJ\\!^P}W_47XG\'M@.&3IQ[', "", alphabet=ALPHA68)
S3 = Rotor('QE\'Y9^6VM.GT<W;>:]8)-%I{+_PH#FB=,N"O@D~S*ZK7(!1`J$4[|L3?C&A\\/2U05R}X', "", alphabet=ALPHA68)
S4 = Rotor('GK/JCT]2#<=4;1_&0EA@3`OD!Z8%)-7{F:H+X?IN,>L.9Q^\'*S[(~$VW"UBRY|\\5P6}M', "", alphabet=ALPHA68)
S5 = Rotor('X5:RC+-Y%A^<@*7GB9=}IFM"Z~[/({LE4P`QV_O1&.2WK!J)]TU#SD;3$06?NH8,\\|>\'', "", alphabet=ALPHA68)
S6 = Rotor('A>Q.5(|R<N-+)1=\'Y}`^JTE9"@[W06UZ%K3\\_BXP7C8{$IHLGS?~:MV,/]F!#D*2&4O;', "", alphabet=ALPHA68)
S7 = Rotor(';IOC6+-D".*?9|LGJ$,U#S0>8:F}1!<]~QVR4[&P=TX^3%\'\\ZB5`M/7N2EK_)WA(HY@{', "", alphabet=ALPHA68)
S8 = Rotor(',&]93BIS=40"{\'D#^K6\\QGXNA1C>F;%JYR[$-!/|.HT@W+ULV<M}()`Z78_O5EP?*:2~', "", alphabet=ALPHA68)
S9 = Rotor('$]E)(6:">Z?PQVSC,XJ7/|U}GT%#[*O5;^&`HM+FK.Y=\\<!D_W\'L42NR0~@-A91I{38B', "", alphabet=ALPHA68)
S10 = Rotor('<{]EQI?*R\\}6!H85D>0,P1=A&#FMV%[\'4N~9+-@/Y.7BT;"L:W$(`C3XZ_U|OK^JS)2G', "", alphabet=ALPHA68)
S11 = Rotor('!{#}MY5*[PW>]2(8R4=VK0_;SZ1^A<J@,D~H.OTG/9+3QN`?6&FI%BL|UC:E-)7$X"\'\\', "", alphabet=ALPHA68)
S12 = Rotor('?PXQ.HL0$<O"+T#E5M1][V8(`@FS\\;Z|}*9^6,BY32KR_/~&\'!G>J-=)W7{:UNA4DCI%', "", alphabet=ALPHA68)
S13 = Rotor('}T=A&_58"49#\'O<:1%Z7]BDNQ.{SM/X!*-G$3RU`[F+VHIK,?@E26(~)^J|W>PLCY\\0;', "", alphabet=ALPHA68)
S14 = Rotor('{P8=,72TU&^?[J}!*;GDY\'$R>CL6514+/@#9]_W3<HIAZ(K-0OQ`:N%|\\FEVM.SB~X")', "", alphabet=ALPHA68)
S15 = Rotor('J-T#R6>O&^;*V?=8:M4F.A[@0I%!~P)]"W|LU{B(2S`\'DX,K\\}Q<7ZYE/CG53$9HN+1_', "", alphabet=ALPHA68)
S16 = Rotor('&-^5%[N`\'J9{=K>W1O|+<G~AR(Z2PU\\SL_,QYI!E"})C6]87@:;?#F4/$D0H*B3TVXM.', "", alphabet=ALPHA68)
S17 = Rotor('81~`)_:S-6&^C<=Z+MG">L]QJ,YTV.@AKBNDHX\\\'37*0{}9R/[O$P(?2#5;|I4E%!UFW', "", alphabet=ALPHA68)
S18 = Rotor('3#Y|F5]%+~9V^?0UB4A/NL=Z<DQ}O!$.@XJ:CHR\'G*MPE6I;7,&(S8\\>KW{-`"1T2)[_', "", alphabet=ALPHA68)
S19 = Rotor(')[{JP,8U2.LMW*X/ZE#36&9%SY<-R(VAH+4@`!|;D?IGN:$KO~B]=C>\\5_\'TQ1^}"F07', "", alphabet=ALPHA68)
S20 = Rotor('`3Z">51I\\HQL;/R.T}D]J?=F^YE0B|-!)S+K8C*#V2{7&~94$<N@[6\'%(PWAG,_MU:XO', "", alphabet=ALPHA68)

J = Reflector('/->)2]^#0!RP19=L7K3UT5Z%`WIMES}V+Q\\NHAJ*?X{[:D"6|B~(<;OC$_\'8FG@Y&,4.', alphabet=ALPHA68)
K = Reflector('>,#;GLE}ZN)F{JY5<49~\\WV1OI|X]_RP=@!SC-8[&?$:^K`.B/+\'DQ6A%7"U2(3*M0HT', alphabet=ALPHA68)
L = Reflector('[F0?~BT1N]#W}I,QP)VG2SL-*8CHU76\'43Z(K^<.&\\$59RY:OX"+=!;_D{A%J/>|@`ME', alphabet=ALPHA68)
M = Reflector('L*W2[TI@G+=A\\.5Z67|F^3C4}P`~DVXOQR&:,({)-?8_/"BJ#$N9>]K;%HEM<U\'0!SY1', alphabet=ALPHA68)
N = Reflector('Q<4!X@6}$-_+/.5:A?#(\\3[E7|&~)VCOGY^\'SMD=I{09T2>L;JNP,B"*RFWU`8K]%ZH1', alphabet=ALPHA68)
O = Reflector('Q^RK]-M3T#D&G[WVAC%I>PO+\\"=(5H!2*|/?J84Z@SL.1<6X_F\';:)0U9$NYEB,{`7~}', alphabet=ALPHA68)
P = Reflector('ODHB3&NCQ"~1|GA)I+`:}>-@?0ZL*E=\']{,#9[$J!(F5%P2R8W<T^.4VYX/_6;\\S7MUK', alphabet=ALPHA68)
Q = Reflector('.N~7OQ\'3;/X0&BE)F6(>8=|K,{L4:H1[RDU^-J$+!`MGSP<"Y#A2I*VT_\\5@}9?%ZW]C', alphabet=ALPHA68)
R = Reflector('M(O%G+E59R_]A3C0^J8\\&Y"<V\'P6=N#H1$SI4.)W7DUZB!?F>|/{@X2,*;`TLQK[:-~}', alphabet=ALPHA68)
S = Reflector(';HPT)_*B}2:Y\'%-C\\3XD4!5SL67>JRUWZ0=<|^V(`N?M"EG,+O]KA981&~{Q./F$[#I@', alphabet=ALPHA68)

# Build the lookup dicts -------------------------------------------------

base_rotors: Dict[str, Rotor] = {
    "I": I, "II": II, "III": III, "IV": IV, "V": V, "VI": VI, "VII": VII,
    "R1": R1, "R2": R2, "R3": R3, "R4": R4, "R5": R5,
    "R6": R6, "R7": R7, "R8": R8, "R9": R9, "R10": R10,
    "S1": S1, "S2": S2, "S3": S3, "S4": S4, "S5": S5,
    "S6": S6, "S7": S7, "S8": S8, "S9": S9, "S10": S10,
    "S11": S11, "S12": S12, "S13": S13, "S14": S14, "S15": S15,
    "S16": S16, "S17": S17, "S18": S18, "S19": S19, "S20": S20,
}

base_reflectors: Dict[str, Reflector] = {
    "A": A, "B": B, "C": C, "D": D, "E": E, "F": F, "G": G, "H": H,
    "J": J, "K": K, "L": L, "M": M, "N": N, "O": O, "P": P,
    "Q": Q, "R": R, "S": S,
}

rotor_dict: Dict[str, Rotor] = {}
for name, obj in base_rotors.items():
    rotor_dict[name] = rotor_dict[name.lower()] = obj  # uppercase + alias

reflector_dict: Dict[str, Reflector] = {}
for name, obj in base_reflectors.items():
    reflector_dict[name] = reflector_dict[name.lower()] = obj

__all__ = ["rotor_dict", "reflector_dict", "ALPHA26", "ALPHA38", "ALPHA68"]