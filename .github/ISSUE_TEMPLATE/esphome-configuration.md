name: ESPHome Configuration
description: Provide the configuration for your ESPHome device
title: '[ESPHome] New Configuration Request'
labels: esphome
assignees: ''

body:
  - type: markdown
    attributes:
      value: |
        # ESPHome Configuration Request
        
        ## Device Network Configuration
  - type: input
    id: device_name
    attributes:
      label: Device Name
      description: Provide the device name for your ESPHome device (e.g., tubeszb-cc2652p2-2023 or tubeszb-cc2652p7-2023)
    validations:
      required: true
  - type: input
    id: static_ip
    attributes:
      label: Static IP
      description: Provide the static IP for your ESPHome device
    validations:
      required: true
  - type: input
    id: gateway
    attributes:
      label: Gateway
      description: Provide the gateway for your ESPHome device
    validations:
      required: true
  - type: input
    id: subnet
    attributes:
      label: Subnet
      description: Provide the subnet for your ESPHome device
    validations:
      required: true
  - type: markdown
    attributes:
      value: |
        ## Additional Information
        Provide any additional details here.
