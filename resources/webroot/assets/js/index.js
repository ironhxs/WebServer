window.onload = function(){
    document.querySelector("#networkSecurityTop").onclick = function(){
        document.querySelector("#networkSecurityResponse").scrollIntoView(true);
    }
    document.querySelector("#ThreatsAndAttacksTop").onclick = function(){
        document.querySelector("#ThreatsAndAttacksResponse").scrollIntoView(true);
    }
    document.querySelector("#SolutionTop").onclick = function(){
        document.querySelector("#SolutionResponse").scrollIntoView(true);
        
    }
    document.querySelector("#DataProtectionAndPrivacyTop").onclick = function(){
        document.querySelector("#DataProtectionAndPrivacyResponse").scrollIntoView(true);
    }
    document.querySelector("#commonProblemTop").onclick = function(){
        document.querySelector("#commonProblemResponse").scrollIntoView(true);
    }
}
var NetworkAttackRealtimeGraphSectionButton = document.getElementById("NetworkAttackRealtimeGraphSectionButton");
var NetworkAttackRealtimeGraphSection = document.getElementById("NetworkAttackRealtimeGraphSection");
var NetworkAttackRealtimeGraph = document.getElementById("NetworkAttackRealtimeGraph");
NetworkAttackRealtimeGraph.addEventListener("click", function() {
    NetworkAttackRealtimeGraphSection.style.display = "block";
  });
  NetworkAttackRealtimeGraphSectionButton.addEventListener("click", function() {
    NetworkAttackRealtimeGraphSection.style.display = "none";
  });
  function redirectToPage1() {
    window.location.href = "ThreatsAndAttacks.html";
  }

  function redirectToPage2() {
    window.location.href = "ThreatsAndAttacks.html";
  }

  function redirectToPage3() {
    window.location.href = "ThreatsAndAttacks.html";
  }

  function redirectToPage4() {
    window.location.href = "ThreatsAndAttacks.html";
  }