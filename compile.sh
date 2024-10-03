xxd -i prompt.rubish > prompt.c
cc -DINCLUDE_PROMPT -g rubish.c -o rdebug
cc -DINCLUDE_PROMPT -O2 rubish.c -o rubish
