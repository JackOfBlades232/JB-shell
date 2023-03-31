parse_command.o: src/interpreter/parse_command.c \
  src/interpreter/parse_command.h src/interpreter/../tokeniz/word_list.h \
  src/interpreter/../tokeniz/word.h \
  src/interpreter/../cmd_data/pipe_seq.h \
  src/interpreter/../cmd_data/cmd_pipe.h \
  src/interpreter/../cmd_data/command.h
interpreter.o: src/interpreter/interpreter.c \
  src/interpreter/interpreter.h src/interpreter/../tokeniz/word_list.h \
  src/interpreter/../tokeniz/word.h src/interpreter/parse_command.h \
  src/interpreter/../cmd_data/pipe_seq.h \
  src/interpreter/../cmd_data/cmd_pipe.h \
  src/interpreter/../cmd_data/command.h \
  src/interpreter/execute_command.h src/interpreter/cmd_res.h
execute_command.o: src/interpreter/execute_command.c \
  src/interpreter/execute_command.h \
  src/interpreter/../tokeniz/word_list.h \
  src/interpreter/../tokeniz/word.h \
  src/interpreter/../cmd_data/pipe_seq.h \
  src/interpreter/../cmd_data/cmd_pipe.h \
  src/interpreter/../cmd_data/command.h src/interpreter/cmd_res.h \
  src/interpreter/parse_command.h src/interpreter/../utils/int_set.h
command.o: src/cmd_data/command.c src/cmd_data/command.h
pipe_seq.o: src/cmd_data/pipe_seq.c src/cmd_data/pipe_seq.h \
  src/cmd_data/cmd_pipe.h src/cmd_data/command.h
cmd_pipe.o: src/cmd_data/cmd_pipe.c src/cmd_data/cmd_pipe.h \
  src/cmd_data/command.h
string_set.o: src/utils/string_set.c src/utils/string_set.h
int_set.o: src/utils/int_set.c src/utils/int_set.h
lookup.o: src/edit/lookup.c src/edit/lookup.h \
  src/edit/../utils/string_set.h
autocomplete.o: src/edit/autocomplete.c src/edit/autocomplete.h \
  src/edit/input.h src/edit/lookup.h src/edit/../utils/string_set.h
input.o: src/edit/input.c src/edit/input.h src/edit/autocomplete.h
word.o: src/tokeniz/word.c src/tokeniz/word.h
line_tokenization.o: src/tokeniz/line_tokenization.c \
  src/tokeniz/line_tokenization.h src/tokeniz/word_list.h \
  src/tokeniz/word.h src/tokeniz/../edit/input.h
word_list.o: src/tokeniz/word_list.c src/tokeniz/word_list.h \
  src/tokeniz/word.h
