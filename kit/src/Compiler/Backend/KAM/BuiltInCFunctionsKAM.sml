(* This file is auto-generated with Tools/GenOpcodes on *)
signature BUILT_IN_C_FUNCTIONS_KAM = 
  sig
    val name_to_built_in_C_function_index : string -> int
  end

functor BuiltInCFunctionsKAM () : BUILT_IN_C_FUNCTIONS_KAM = 
  struct
    fun name_to_built_in_C_function_index name =
      case name
        of "stdErrStream" => 0
      | "stdOutStream" => 1
      | "stdInStream" => 2
      | "sqrtFloat" => 3
      | "lnFloat" => 4
      | "negInfFloat" => 5
      | "posInfFloat" => 6
      | "sml_getrutime" => 7
      | "sml_getrealtime" => 8
      | "sml_localoffset" => 9
      | "exnName" => 10
      | "printReal" => 11
      | "printString" => 12
      | "printNum" => 13
      | "printList" => 14
      | "implodeChars" => 15
      | "implodeString" => 16
      | "concatString" => 17
      | "sizeString" => 18
      | "subString" => 19
      | "div_int_" => 20
      | "mod_int_" => 21
      | "word_sub0" => 22
      | "word_update0" => 23
      | "word_table0" => 24
      | "table_size" => 25
      | "allocString" => 26
      | "updateString" => 27
      | "chrChar" => 28
      | "greaterString" => 29
      | "lessString" => 30
      | "lesseqString" => 31
      | "greatereqString" => 32
      | "equalString" => 33
      | "div_word_" => 34
      | "mod_word_" => 35
      | "quotInt" => 36
      | "remInt" => 37
      | "divFloat" => 38
      | "sinFloat" => 39
      | "cosFloat" => 40
      | "atanFloat" => 41
      | "asinFloat" => 42
      | "acosFloat" => 43
      | "atan2Float" => 44
      | "expFloat" => 45
      | "powFloat" => 46
      | "sinhFloat" => 47
      | "coshFloat" => 48
      | "tanhFloat" => 49
      | "floorFloat" => 50
      | "ceilFloat" => 51
      | "truncFloat" => 52
      | "stringOfFloat" => 53
      | "isnanFloat" => 54
      | "realInt" => 55
      | "generalStringOfFloat" => 56
      | "closeStream" => 57
      | "openInStream" => 58
      | "openOutStream" => 59
      | "openAppendStream" => 60
      | "flushStream" => 61
      | "outputStream" => 62
      | "inputStream" => 63
      | "lookaheadStream" => 64
      | "openInBinStream" => 65
      | "openOutBinStream" => 66
      | "openAppendBinStream" => 67
      | "sml_errormsg" => 68
      | "sml_errno" => 69
      | "sml_access" => 70
      | "sml_getdir" => 71
      | "sml_isdir" => 72
      | "sml_mkdir" => 73
      | "sml_chdir" => 74
      | "sml_readlink" => 75
      | "sml_islink" => 76
      | "sml_realpath" => 77
      | "sml_devinode" => 78
      | "sml_rmdir" => 79
      | "sml_tmpnam" => 80
      | "sml_modtime" => 81
      | "sml_filesize" => 82
      | "sml_remove" => 83
      | "sml_rename" => 84
      | "sml_settime" => 85
      | "sml_opendir" => 86
      | "sml_readdir" => 87
      | "sml_rewinddir" => 88
      | "sml_closedir" => 89
      | "sml_system" => 90
      | "sml_getenv" => 91
      | "terminate" => 92
      | "sml_commandline_name" => 93
      | "sml_commandline_args" => 94
      | "sml_localtime" => 95
      | "sml_gmtime" => 96
      | "sml_mktime" => 97
      | "sml_asctime" => 98
      | "sml_strftime" => 99
      | _ => ~1
  end
