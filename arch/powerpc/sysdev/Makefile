## $Id: Makefile 5088 2010-02-09 02:49:16Z Sawada Koji $

ifeq ($(CONFIG_PPC64),y)
EXTRA_CFLAGS			+= -mno-minimal-toc
endif

mpic-msi-obj-$(CONFIG_PCI_MSI)	+= mpic_msi.o mpic_u3msi.o mpic_pasemi_msi.o
obj-$(CONFIG_MPIC)		+= mpic.o $(mpic-msi-obj-y)
fsl-msi-obj-$(CONFIG_PCI_MSI)	+= fsl_msi.o

obj-$(CONFIG_PPC_MPC106)	+= grackle.o
obj-$(CONFIG_PPC_DCR_NATIVE)	+= dcr-low.o
obj-$(CONFIG_PPC_PMI)		+= pmi.o
obj-$(CONFIG_U3_DART)		+= dart_iommu.o
obj-$(CONFIG_MMIO_NVRAM)	+= mmio_nvram.o
obj-$(CONFIG_FSL_SOC)		+= fsl_soc.o
obj-$(CONFIG_FSL_PCI)		+= fsl_pci.o $(fsl-msi-obj-y)
obj-$(CONFIG_FSL_LBC)		+= fsl_lbc.o
obj-$(CONFIG_FSL_GTM)		+= fsl_gtm.o
obj-$(CONFIG_RAPIDIO)		+= fsl_rio.o
obj-$(CONFIG_TSI108_BRIDGE)	+= tsi108_pci.o tsi108_dev.o
obj-$(CONFIG_QUICC_ENGINE)	+= qe_lib/
obj-$(CONFIG_PPC_BESTCOMM)	+= bestcomm/
mv64x60-$(CONFIG_PCI)		+= mv64x60_pci.o
obj-$(CONFIG_MV64X60)		+= $(mv64x60-y) mv64x60_pic.o mv64x60_dev.o \
				   mv64x60_udbg.o
obj-$(CONFIG_RTC_DRV_CMOS)	+= rtc_cmos_setup.o
obj-$(CONFIG_AXON_RAM)		+= axonram.o

obj-$(CONFIG_PPC_INDIRECT_PCI)	+= indirect_pci.o
obj-$(CONFIG_PPC_I8259)		+= i8259.o
obj-$(CONFIG_IPIC)		+= ipic.o
obj-$(CONFIG_PCI_MSI)		+= mpc83xx_msi.o
obj-$(CONFIG_4xx)		+= uic.o
obj-$(CONFIG_4xx_SOC)		+= ppc4xx_soc.o
obj-$(CONFIG_XILINX_VIRTEX)	+= xilinx_intc.o
obj-$(CONFIG_OF_RTC)		+= of_rtc.o
ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_4xx)		+= ppc4xx_pci.o
endif

# 2009/12/1, Added by Panasonic
obj-$(CONFIG_PPC_83xx_GPIO) += mpc83xx_gpio.o
obj-$(CONFIG_P2PF) += p2pf/

# Temporary hack until we have migrated to asm-powerpc
ifeq ($(ARCH),powerpc)
obj-$(CONFIG_CPM)		+= cpm_common.o
obj-$(CONFIG_CPM2)		+= cpm2.o cpm2_pic.o
obj-$(CONFIG_QUICC_ENGINE)	+= cpm_common.o
obj-$(CONFIG_PPC_DCR)		+= dcr.o
obj-$(CONFIG_8xx)		+= mpc8xx_pic.o cpm1.o
obj-$(CONFIG_UCODE_PATCH)	+= micropatch.o
endif

ifeq ($(CONFIG_SUSPEND),y)
obj-$(CONFIG_6xx)		+= 6xx-suspend.o
endif

ifeq ($(CONFIG_SUSPEND),y)
obj-$(CONFIG_6xx)		+= 6xx-suspend.o
endif
