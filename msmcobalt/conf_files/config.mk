ifeq ($(TARGET_BOARD_PLATFORM),msm8937)
PRODUCT_COPY_FILES += $(QCOM_MEDIA_ROOT)/conf_files/msm8937/media_profiles_8937.xml:system/etc/media_profiles.xml \
                      $(QCOM_MEDIA_ROOT)/conf_files/msm8937/media_codecs_8937.xml:system/etc/media_codecs.xml \
                      $(QCOM_MEDIA_ROOT)/conf_files/msm8937/media_codecs_performance_8937.xml:system/etc/media_codecs_performance.xml
endif #TARGET_BOARD_PLATFORM
