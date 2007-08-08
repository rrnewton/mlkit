
structure ExecutionBarry : EXECUTION =
  struct
    structure Compile = CompileBarry
    structure TopdecGrammar = PostElabTopdecGrammar
    structure PP = PrettyPrint
    structure Labels = AddressLabels

    structure DecGrammar = TopdecGrammar.DecGrammar

    structure CompileBasis = CompileBasisBarry

    val backend_name = "Barry"
    val backend_longname = "Barry - the Standard ML barifier"

    type CompileBasis = CompileBasis.CompileBasis
    type CEnv = CompilerEnv.CEnv
    type Env = CompilerEnv.ElabEnv
    type strdec = TopdecGrammar.strdec
    type strexp = TopdecGrammar.strexp
    type funid = TopdecGrammar.funid
    type strid = TopdecGrammar.strid
    type lab = Labels.label
    type linkinfo = {unsafe:bool}
    type target = Compile.target

    val pr_lab = Labels.pr_label
    val dummy_label = Labels.new()
    val code_label_of_linkinfo : linkinfo -> lab = fn _ => dummy_label
    val imports_of_linkinfo : linkinfo -> (lab list * lab list) = fn _ => (nil,nil)
    val exports_of_linkinfo : linkinfo -> (lab list * lab list) = fn _ => (nil,nil)
    fun unsafe_linkinfo (li: linkinfo) : bool =  #unsafe li

    (* Hook to be run before any compilation *)
    val preHook = Compile.preHook
	
    (* Hook to be run after all compilations (for one compilation unit) *)
    val postHook = Compile.postHook

    datatype res = CodeRes of CEnv * CompileBasis * target * linkinfo
                 | CEnvOnlyRes of CEnv
    fun compile fe (ce,CB,strdecs,vcg_file) =
      let val (cb,()) = CompileBasis.de_CompileBasis CB
      in
	case Compile.compile fe (ce, cb, strdecs)
	  of Compile.CEnvOnlyRes ce => CEnvOnlyRes ce
	   | Compile.CodeRes(ce,cb,target,safe) => 
	    let 
		(* to use not(safe) below, we should compile lvars, etc., to labels and 
		 * use imports and exports appropriately *)
		val linkinfo : linkinfo = {unsafe=true(*not(safe)*)}   
		val CB = CompileBasis.mk_CompileBasis(cb,())
	    in CodeRes(ce,CB,target,linkinfo)
	    end
      end
    val generate_link_code = NONE
    fun emit a = Compile.emit a
    fun link_files_with_runtime_system files run =
	let val pm_file = run ^ ".mlb"
	    val os = TextIO.openOut pm_file
	in 
	    (TextIO.output(os, "(* Generated by " ^ backend_longname ^ " *)\n\n");
	     app (fn f => TextIO.output(os, f ^ "\n")) files;
	     TextIO.closeOut os;
	     print("[Created file " ^ pm_file ^ "]\n"))
	    handle X => (TextIO.closeOut os; raise X)
	end

    fun mlbdir() = "MLB" ## "Barry"

    val pu_linkinfo =
	Pickle.convert (fn b => {unsafe=b},
			fn {unsafe=b} => b)
	Pickle.bool
  end

