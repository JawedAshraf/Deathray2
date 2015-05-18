Deathray2
=========

An Avisynth plug-in filter for spatial/temporal non-local means de-noising.

Created by Jawed Ashraf - Deathray@cupidity.f9.co.uk


Installation
============

Copy the Deathray2.dll to the "plugins" sub-folder of your installation of 
Avisynth.


De-installation
===============

Delete the Deathray2.dll from the "plugins" sub-folder of your installation of 
Avisynth.


Compatibility
=============

The following software configurations are known to work:

 - Avisynth 2.5.8         and 2.6 MT (SEt's)

The following hardware configurations are known to work:

 - AMD HD 7770
 - AMD HD 7970

Known non-working hardware:

 - ATI cards in the 4000 series or earlier
 - ATI cards in the 5400 series

Video:

 - Deathray2 is compatible solely with 8-bit planar formatted video. It has
   been tested with YV12 format.


Usage
=====

Deathray2 separates the video into its 3 component planes and processes each
of them independently. This means some parameters come in two flavours: luma
and chroma.

Filtering can be adjusted with the following parameters, with the default 
value for each in brackets:

 hY  (1.0) - strength of de-noising in the luma plane.

             Cannot be negative.

             If set to 0 Deathray2 will not process the luma plane.

 hUV (1.0) - strength of de-noising in the chroma planes.

             Cannot be negative.

             If set to 0 Deathray2 will not process the chroma planes.

 tY  (0)   - temporal radius for the luma plane.

             Limited to the range 0 to 64.

             When set to 0 spatial filtering is performed on the 
             luma plane. When set to 1 filtering uses the prior,
             current and next frames for the non-local sampling
             and weighting process. Higher values will increase
             the range of prior and next frames that are included.

 tUV (0)   - temporal radius for the chroma planes.

             Limited to the range 0 to 64.

             When set to 0 spatial filtering is performed on the 
             chroma planes. When set to 1 filtering uses the prior,
             current and next frames for the non-local sampling
             and weighting process. Higher values will increase
             the range of prior and next frames that are included.

 s   (1.0) - sigma used to generate the gaussian weights.

             Limited to values of at least 0.1.

             The kernel implemented by Deathray2 uses 7x7-pixel 
             windows centred upon the pixel being filtered. 

             For a 2-dimensional gaussian kernel sigma should be 
             approximately 1/3 of the radius of the kernel, or less,
             to retain its gaussian nature. 

             Since a 7x7 window has a radius of 3, values of sigma 
             greater than 1.0 will tend to bias the kernel towards
             a box-weighting. i.e. all pixels in the window will 
             tend towards being equally weighted. This will tend to 
             reduce the selectivity of the weighting process and 
             result in relatively stronger spatial blurring.

 x   (1)   - factor to expand sampling.

             Limited to values in the range 1 to 4.

             By default Deathray2 spatially samples 49 windows 
             centred upon the pixel being filtered, in a 7x7
             arrangement. x increases the sampling range in
             multiples of the kernel radius.
             
             Since the kernel radius is 3, setting x to 2 produces
             a sampling area of 13x13, i.e. 169 windows centred
             upon the target pixel. Yet higher values of x such as
             3 or 4 will result in 19x19 or 25x25 sample windows.

 l (false) - redundant option to be removed

 c (true)  - redundant option to be removed
			 
 z (false) - redundant option to be removed

 b (false) - option non-functional
 
 a   (8)   - alpha sample set size.
 
             limited to values in the range 8 to 128, with values
			 rounded down to the nearest multiple of 8.
 
			 Deathray2 sorts the samples in order to exclude the
			 worst samples. This improves detail retention while
			 enabling strong filtering.
			 
			 
Avisynth MT
===========

Deathray2 is not thread safe. This means that only a single instance of
Deathray2 can be used per Avisynth script. By extension this means that
it is not compatible with any of the multi-threading modes of the 
Multi Threaded variant of Avisynth. 

Use:

SetMTMode(5) 

before a call to Deathray2 in the Avisynth script, if multi-threading
is active in other parts of the script.


Multiple Scripts Using Deathray2
================================

The graphics driver is thread safe. This means it is possible to have
an arbitrary number of Avisynth scripts calling Deathray2 running on a 
system. 

e.g. 2 scripts could be encoding, another could be running in a media player
and another could be previewing individual frames in AvsP or VirtualDub.

Eventually video memory will probably run out, even though it's virtualised.
