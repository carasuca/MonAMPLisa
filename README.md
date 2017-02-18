# MonAMPLisa
inspired by Gynvael's https://youtu.be/7zI7M_5_jBE

## Does
* all on GPU,
* support 8-bit mono png/bmp/ico/gifs,
* fixed crossover (mean value).

# Goes
* 17.2 ms per generation @ 540M
*  2.6 ms per generation @ R2 700 

## Uses
* [Assisted Massive Parallelism](https://msdn.microsoft.com/en-us/library/hh265137.aspx)
* [Windows Imaging Component](https://msdn.microsoft.com/en-us/library/windows/desktop/ee719902.aspx)
* [MSXML](https://msdn.microsoft.com/en-us/library/ms753791.aspx)(?!) for sending HTTP requests because oh why not?
