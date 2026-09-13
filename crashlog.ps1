Get-WinEvent -FilterHashtable @{LogName='Application'; Level=2} -MaxEvents 500 |
  Where-Object { $_.Message -match 'Scanthia|Scintra' } |
  Select-Object -First 4 TimeCreated, Message | Format-List
