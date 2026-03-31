To be done

For the Tracking Tab, after importing, modify the carrierID insert this function =VLOOKUP($A2,ListCarriers!A:B,2,FALSE)

In the carrier colum, create Data Validation (Data-> Data Validation)
Apply to range: Tracking!A2:A20
Criteria: DropDown
Range: =ListCarriers!$A$1:$A$2001
