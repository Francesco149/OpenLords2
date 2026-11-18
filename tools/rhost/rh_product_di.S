/* the product's data image (rhost.c -DRH_PRODUCT, di_load "(embedded)"): .rdata / .data composed from the port's own
 * definitions (tools/data_selfcheck.py --compose --strict → build/rl_data.img, OL2DATA1), linked in as read-only data */
    .section .rdata,"dr"
    .balign 16
    .globl _rh_product_di
_rh_product_di:
    .incbin DI_PATH
    .globl _rh_product_di_end
_rh_product_di_end:
