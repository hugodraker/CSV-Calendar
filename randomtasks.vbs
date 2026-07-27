' ============================================================================
' Calendar Generator Script
' PUBLIC DOMAIN - Dedicated to the public domain (CC0 / No Rights Reserved)
' Creates a CSV file with distributed events across multiple people for a full year.
' ============================================================================

Option Explicit

Dim fso, outputFile, strInput, eventsPerDay, numPeople, i, personIdx
Dim taskNames(), startTimes(), colorCodes(), currentDate, dtCurrent
Dim taskIndex, startTime, colorCode, totalEvents, totalDays
Dim baseYear, baseMonth, baseDay, dayIndex, eventOnThisDay, eventID
Dim versionVal, lastModifiedBy

' Initialize File System Object
Set fso = CreateObject("Scripting.FileSystemObject")

' Get user input with safety validation
strInput = InputBox("How many events per day?", "Events Per Day", "5")
If Not IsNumeric(strInput) Then WScript.Quit 1
eventsPerDay = CInt(strInput)
If eventsPerDay <= 0 Then WScript.Quit 1

strInput = InputBox("How many people?", "Person Count", "7")
If Not IsNumeric(strInput) Then WScript.Quit 1
numPeople = CInt(strInput)
If numPeople <= 0 Then WScript.Quit 1

' Set full year duration (365 days)
totalDays = 365
totalEvents = eventsPerDay * totalDays

' Load 50 office and project task descriptions (Indices 0 to 49)
ReDim taskNames(49)
taskNames(0)  = "Team Standup Meeting"
taskNames(1)  = "Quarterly Budget Review"
taskNames(2)  = "Client Presentation Prep"
taskNames(3)  = "Project Sprint Planning"
taskNames(4)  = "Performance Review Session"
taskNames(5)  = "Vendor Contract Discussion"
taskNames(6)  = "Code Review Workshop"
taskNames(7)  = "Marketing Campaign Sync"
taskNames(8)  = "Training & Development"
taskNames(9)  = "Product Roadmap Review"
taskNames(10) = "Security Audit Meeting"
taskNames(11) = "Department Budget Planning"
taskNames(12) = "Customer Feedback Analysis"
taskNames(13) = "Infrastructure Upgrade Plan"
taskNames(14) = "Hiring Interview Round"
taskNames(15) = "Compliance Training Session"
taskNames(16) = "Cross-team Collaboration"
taskNames(17) = "Sales Pipeline Review"
taskNames(18) = "Technical Debt Assessment"
taskNames(19) = "OKR Progress Tracking"
taskNames(20) = "New Feature Brainstorming"
taskNames(21) = "Risk Management Workshop"
taskNames(22) = "Stakeholder Update Call"
taskNames(23) = "Data Migration Planning"
taskNames(24) = "Employee Onboarding"
taskNames(25) = "Partnership Discussion"
taskNames(26) = "Quality Assurance Review"
taskNames(27) = "Innovation Lab Session"
taskNames(28) = "Annual Strategy Planning"
taskNames(29) = "Team Building Activity"
taskNames(30) = "Architecture Review Board"
taskNames(31) = "Database Index Optimization"
taskNames(32) = "API Refactoring Sync"
taskNames(33) = "Incident Post-Mortem"
taskNames(34) = "UI/UX Usability Testing"
taskNames(35) = "Cloud Cost Optimization Sync"
taskNames(36) = "DevOps Pipeline Audit"
taskNames(37) = "Disaster Recovery Drill"
taskNames(38) = "Third-party Integration Review"
taskNames(39) = "Localization & Translation Sync"
taskNames(40) = "Accessibility Compliance Audit"
taskNames(41) = "Executive Steering Committee"
taskNames(42) = "Customer Success Handoff"
taskNames(43) = "System Performance Tuning"
taskNames(44) = "Product Backlog Refinement"
taskNames(45) = "Design System Governance"
taskNames(46) = "Release Readiness Sign-off"
taskNames(47) = "Cybersecurity Threat Modeling"
taskNames(48) = "Mobile App Beta Testing"
taskNames(49) = "Data Privacy & GDPR Review"

' Define start times in minutes from midnight (Indices 0 to 6)
ReDim startTimes(6)
startTimes(0) = 540  ' 9:00 AM
startTimes(1) = 600  ' 10:00 AM
startTimes(2) = 660  ' 11:00 AM
startTimes(3) = 780  ' 1:00 PM
startTimes(4) = 840  ' 2:00 PM
startTimes(5) = 900  ' 3:00 PM
startTimes(6) = 960  ' 4:00 PM

' Define color codes matching application format (Indices 0 to 6)
ReDim colorCodes(6)
colorCodes(0) = "15047427"
colorCodes(1) = "7976499"
colorCodes(2) = "11150478"
colorCodes(3) = "1987060"
colorCodes(4) = "7568614"
colorCodes(5) = "15047427"
colorCodes(6) = "7976499"

' Create output file
Set outputFile = fso.CreateTextFile("calendar.csv", True)

' Write header with pipe delimiter (¦)
outputFile.WriteLine "ID¦Title¦StartMin¦Duration¦Color¦Date¦PersonIdx¦Version¦LastModifiedBy"

' Base start date: January 1, 2026
baseYear = 2026
baseMonth = 1
baseDay = 1

For i = 0 To totalEvents - 1
    ' Prevent 32-bit Integer Overflow error on 14-digit IDs
    eventID = FormatNumber(CDbl(17851576000000) + i, 0, 0, 0, 0)
    
    dayIndex = i \ eventsPerDay
    eventOnThisDay = i Mod eventsPerDay
    
    ' Rotate PersonIdx smoothly across days and daily event slots
    personIdx = (eventOnThisDay + dayIndex) Mod numPeople
    
    ' Vary Version (1 through 5) and LastModifiedBy (assignee or collaborator)
    versionVal = ((i * 3) Mod 5) + 1
    lastModifiedBy = (personIdx + (i Mod 3)) Mod numPeople
    
    ' Select task, start time, and color safely within array bounds
    taskIndex = i Mod (UBound(taskNames) + 1)
    startTime = startTimes((eventOnThisDay + dayIndex) Mod (UBound(startTimes) + 1))
    colorCode = colorCodes(personIdx Mod (UBound(colorCodes) + 1))
    
    ' Calculate date across full year using DateSerial and DateAdd
    dtCurrent = DateAdd("d", dayIndex, DateSerial(baseYear, baseMonth, baseDay))
    currentDate = Year(dtCurrent) & "/" & Right("0" & Month(dtCurrent), 2) & "/" & Right("0" & Day(dtCurrent), 2)
    
    ' Write event row
    outputFile.WriteLine eventID & "¦" & taskNames(taskIndex) & "¦" & _
                        startTime & "¦60¦" & colorCode & "¦" & _
                        currentDate & "¦" & personIdx & "¦" & versionVal & "¦" & lastModifiedBy
Next

outputFile.Close

MsgBox "Calendar created successfully!" & vbCrLf & _
       "Events per day: " & eventsPerDay & vbCrLf & _
       "People: " & numPeople & vbCrLf & _
       "Total days: " & totalDays & vbCrLf & _
       "Total events: " & totalEvents & vbCrLf & _
       "Date range: Jan 01, 2026 - Dec 31, 2026" & vbCrLf & _
       "Average events per person: " & FormatNumber(CDbl(totalEvents) / CDbl(numPeople), 1), vbInformation, "Generation Complete"