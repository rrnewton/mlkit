(* Generate Target Code *)
signature CODE_GEN =
  sig

    type label
    type ('sty,'offset,'aty) LinePrg
    type offset = int
    type StoreTypeCO
    type AtySS
    type AsmPrg

    val CG : {main_lab:label,
	      code:(StoreTypeCO,offset,AtySS) LinePrg,
	      imports:label list,
	      exports:label list,
	      safe:bool} -> AsmPrg


    val emit : AsmPrg * string -> unit
    val generate_link_code : label list -> AsmPrg
  end








