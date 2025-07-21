# registry.py
from __future__ import annotations
from typing import Dict

from rotor_and_reflector import Rotor, Reflector   # adjust import path if needed

# ── Core Alphabets (must match rest of system) ───────────────────────────
ALPHA26 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
ALPHA38 = ALPHA26 + "0123456789#/"
ALPHA76 = (
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "0123456789#/"
    "()[]{}<>"
    "+-*^=≈≠\\∇"
    "|_~'\".,;:!?§@&%"
    "$£€¥₹¢"
)

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
S1 = Rotor('|IM+H4"@09:~{-?1EF,(#€/GZR)7U!3}$VJ∇≈BT;K.D]L\\&82><\'¢CXY[PW5N_=6≠₹QSO*§^%¥£A', "", alphabet=ALPHA76)
S2 = Rotor('XH?A+94ON>K§GYC-!(<,U:_1]$¢3J^T@M[8E≠%)PVF05€"~7S}₹≈=I*R6D¥\\2;/QB|W∇Z£.#&\'{L', "", alphabet=ALPHA76)
S3 = Rotor('>\\₹]XA≈P}C,£%¥S#?8W1R_IO$@B¢+J∇≠T"*0FN<~4D7/|€U^YH6§E&V\':Q{3-G;L59[ZK!=).2(M', "", alphabet=ALPHA76)
S4 = Rotor('8"*]>\\}~PB)A-:XJL97≠E5∇₹Y(G,/.£?{[S;0NW_¢T^6K&$DH=21C!F§#Q@<|Z4€+≈\'3VRI%¥OUM', "", alphabet=ALPHA76)
S5 = Rotor('P!~^7"LI€£6(≈3&HUC-;#@:5GQ<WT,ESJ\\?$¥%NB)|₹A+4≠Y_]X¢O>D91/2M\'.0[F∇}=R*8§ZVK{', "", alphabet=ALPHA76)
S6 = Rotor('"^Z¢#=N)\\.$O(V-IM~]%∇LB≈{0+AT7EJ|4WKD3\'F[6H_*₹X2U5Y91£€&S,/G8@}!C¥:≠?>QR;P<§', "", alphabet=ALPHA76)
S7 = Rotor('Y4PV7T0≈-65X$MHU2¢<9{£.">≠F_LRGK?&8^S=[A₹#@IQ∇E:)+€/N\\1|]*Z§O%;D,}!C\'(B~¥JW3', "", alphabet=ALPHA76)
S8 = Rotor('¥?M<YRS:5J¢1BQA*∇T^[I$-|2}§@"PNG%\')!₹7L=€WEV£F~]4O_>#,8KZ≠&CX≈6D{3(\\/+90UH.;', "", alphabet=ALPHA76)
S9 = Rotor('%¢\\8Z@1S4?732.€Y#OG!≈§¥\'UR₹CI]5+$FKP^~B>("<_*};0&≠={∇/£DNEH6JV|L-9X[TA)M:,WQ', "", alphabet=ALPHA76)
S10 = Rotor('CEJNZ@;B9-Q6#HM∇~<_¢]4A\\D*1KR€£TW=08P2+¥/!%.?X7{^|\'5,&G:≠>§U)₹≈3}YF[S"O(V$IL', "", alphabet=ALPHA76)
S11 = Rotor('≠UT|\'4.#Y?6₹KNP@R+1!_L¢C2€,-)&B£V";XGH7D\\^9$MI~J]W{Q>5Z=3%A*[§:∇O/(¥E≈}SF8<0', "", alphabet=ALPHA76)
S12 = Rotor('>[25H1{0VZ|IM∇F,8A@D&#%T¥GCL*U_JN^6E≠X}-¢\\§£€?<3₹.=/K$O4QSY;"~9)≈+\'P(WR]B!:7', "", alphabet=ALPHA76)
S13 = Rotor('J§(PW\\^"EKD%¥A8∇#.H*≠R&2}QB$F/)3;TY4≈~V9]X@!€IM{₹6SUZ:L7=|+¢0GC5N\'-<?[£O1,>_', "", alphabet=ALPHA76)
S14 = Rotor('7%9P<8T~)5&K.|H+>^B≈/4",M\'6¥I₹?E§∇*XU#£JW;Q:=210€{[-≠!DS3R\\_NF$]O}LA¢(Y@CGZV', "", alphabet=ALPHA76)
S15 = Rotor('_,¥M1TFC≈I2.§8[NPEZD≠\\%^∇?V€R¢WB|&;U₹S5X\'JQO*AY/}(G=-{HL3]9>64"7@0$:<~£+#K!)', "", alphabet=ALPHA76)
S16 = Rotor('(HY<7QS3\'4CFX€OAB=≈_9IG{∇?*12>/%;RL+\\]&W).:U5[§}P₹E@!T,≠8DV~Z#MJ6-$N¥K¢£^"|0', "", alphabet=ALPHA76)
S17 = Rotor('5I1£≠~_TEAP7DQ\'\\XUGM{RF4≈B#J!9C3:YO=$¥8L§(€H]^₹2VN?&¢Z-@*,6W∇+|}S[").>K;</0%', "", alphabet=ALPHA76)
S18 = Rotor('≠;\\.8+(5€₹#ZLI∇="W:M4QY¢9*2$0D[AU7NCGE%¥&,!_?<TX>3H~F]1OP@)6\'JK≈}§{RV/^£S|B-', "", alphabet=ALPHA76)
S19 = Rotor('D0&E|\\B;~6$§S9∇H/12]XKU!>=+≠£QN5{.^LM_8JV4(T7₹*,O[¥Z?G"≈}3)-<\'C#P€Y@FW%R:AI¢', "", alphabet=ALPHA76)
S20 = Rotor('/^T+≈5€~B2N%E!={U:#O§-₹8&*SFLD≠71>Y6C9,4¥)X[V"|QWHRMJK3@]\\∇;Z$}_(<£.0\'I?P¢AG', "", alphabet=ALPHA76)

J = Reflector('\\(J≈"8=IHCWX₹!^¥9{>]&$KL∇%5_/430;)FQ@2B7}TR[,S\'≠€OGD-AY.1?+E|<6£N~¢#UZV:*PM§', alphabet=ALPHA76)
K = Reflector('9_Z]}1@(+Q§3T8%"J4?M#0{/≠CVF5LR2-∇NAUXH*¥DWE,=I6)&>¢Y~7.B\\€P|<:;₹SKG^O£$\'[!≈', alphabet=ALPHA76)
L = Reflector('#5[≈_T£\'},%Q4.§XL@|F*/<P6{$-7:MBY2+≠AV^~C\\ZIW?81U(¢D9];SE)H₹NJ∇3€>OR¥K0G!&"=', alphabet=ALPHA76)
M = Reflector('(JEZCU\\;7B¢≈[6*V:§}₹FP3=\'D∇>"W!@NI_%/#A+M^~S,1)$O]XL£G0€8{Y2¥<HQ4&R5?9-≠|.TK', alphabet=ALPHA76)
N = Reflector('{)X~=\'Y6UP9:\\!≠J¥/50I€,CG+T&#[$SH¢∇K2R≈B3.A^£*Z_>}E(OM8;-DF§]W|LN₹"%1@4<VQ?7', alphabet=ALPHA76)
O = Reflector('₹VX.$LTY"[4F{?\'≈§U9GRB=CH6);(-K|Z*+S:¢20J€M\\%_837@WP¥}£5>!OID&1#~NQ^,<E∇]≠A/', alphabet=ALPHA76)
P = Reflector('-CBH\\>}D_≈2;]?V8^4)*"OZ0[WX:K§R₹\'=P,%{<SYM/G(F¢ATQ7J$E€¥I!6U@9L1~N3.£#≠&∇|5+', alphabet=ALPHA76)
Q = Reflector('9->X50M£:+!§G%"¢2;∇V)T]D*6F4Q.1EZ≠\'A=@€U,W|&_CJBY$#~7¥S{<≈8O3[RIK₹L/}N^H(\\?P', alphabet=ALPHA76)
R = Reflector('GR§\'¥UA(ZTYX925>£B|JF#_LKI)$N\\:O*=~MV-H0@?}{∇P,/6≠7.^3<SW8D&≈+%4₹]C[";1Q¢E!€', alphabet=ALPHA76)
S = Reflector('{GV$?*B€#\'\\1|X@W"¥,≈!CPN£8/L32_%.+Z₹I0[;(≠A^§:7¢F}&T]K~M4∇JQ6S)>UE<O=5DYHR9-', alphabet=ALPHA76)

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

__all__ = ["rotor_dict", "reflector_dict", "ALPHA26", "ALPHA38", "ALPHA76"]