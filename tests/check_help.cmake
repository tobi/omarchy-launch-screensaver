if(NOT BIN)
  message(FATAL_ERROR "BIN not set")
endif()

execute_process(
  COMMAND "${BIN}" --help
  RESULT_VARIABLE rc
  OUTPUT_VARIABLE out
  ERROR_VARIABLE err
)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "--help exited ${rc}\n${out}\n${err}")
endif()

foreach(needle IN ITEMS force --input --random-effect --effect --fade)
  string(FIND "${out}" "${needle}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "--help missing '${needle}'\n${out}")
  endif()
endforeach()
