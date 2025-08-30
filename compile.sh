cc rubish.c -o temprubish
./temprubish '
 set fin openin "prompt.rubish"
 set fout openout "prompt.c"
 print fout "unsigned char prompt_rubish[] = { 32,";
 set count 0
 for( i 0 (file-size fin) ) (
  (print fout cat$ (str$ get-byte fin) ",	")
  set count % (+ count 1) 8;
  if ! count (print fout;)
 )
 print fout " 0 };";
'
rm temprubish*
cc -Wall -DINCLUDE_PROMPT -DMATHSLIB -g rubish.c -lm -o rdebug
cc -Wall -DINCLUDE_PROMPT -DMATHSLIB -O2 rubish.c -lm -o rubish
