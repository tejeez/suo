Here's an incomplete receiver implemtnation with the aim of being
somewhat modular, separating different stages of signal processing:

 - Preamble detection, or more generally initial acquisition
 - Demodulation, including tracking of symbol synchronization, etc
 - Deframing

The goal is to have good demodulation performance for weak signals
to make it usable for applications such as satellite communication,
and also to support a wider range of modulations and framing formats.

It is still undergoing some refactoring and does not currently compile.

